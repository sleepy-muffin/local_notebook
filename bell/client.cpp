#include <algorithm>
#include <boost/json/object.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <set>
#include <stop_token>
#include <string_view>
#include <unordered_set>
#include <utility>
#include "common.hpp"
#include "felis/log.hpp"
#include "felis/str.hpp"
#include "felis/sync.hpp"
#include "message.hpp"
#include "store.hpp"
#include "tools.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <boost/json.hpp>
#include <felis/log.hpp>

#include <felis/net/llm.hpp>

#include "application.hpp"
#include "client.hpp"
#include "request.hpp"


// The transport's failure kinds are exactly the vocabulary a retry policy needs,
// so they are preserved as codes instead of being flattened into the reason.
static felis::bell::error_code request_failure_code(felis::net::llm::FailureKind kind) {
  switch (kind) {
    case felis::net::llm::FailureKind::Cancelled:
      return felis::bell::error_code::ERR_CANCELLED;
    case felis::net::llm::FailureKind::HttpStatus:
      return felis::bell::error_code::ERR_REQUEST_HTTP;
    case felis::net::llm::FailureKind::Stream:
      return felis::bell::error_code::ERR_REQUEST_STREAM;
    case felis::net::llm::FailureKind::Transport:
      return felis::bell::error_code::ERR_REQUEST_TRANSPORT;
  }
  return felis::bell::error_code::ERR_REQUEST_TRANSPORT;
}

// Wait for `interval`, but wake immediately when `stop` is requested. Returns
// true when the wait ran its course, false when a cancellation cut it short.
static bool sleep_interruptibly(std::stop_token stop, std::chrono::milliseconds interval) {
  if (interval.count() <= 0) {
    return !stop.stop_requested();
  }

  std::mutex mutex;
  std::condition_variable_any cv;
  std::unique_lock<std::mutex> lock(mutex);

  // The stop callback takes the same mutex, so a request that lands between the
  // check below and the wait cannot be missed.
  std::stop_callback wake(stop, [&cv] { cv.notify_all(); });
  const bool finished = !cv.wait_for(lock, interval, [&stop] { return stop.stop_requested(); });
  return finished;
}

// One streaming POST against the assistant's configured endpoint. The payload
// was already shaped for that endpoint's protocol; the decoded deltas drive
// `callbacks`. On success the finished turn is returned, otherwise
// `std::nullopt` (with the failure kind and reason copied into the out params).
[[nodiscard]] std::optional<felis::net::llm::Result> stream_llm_request(
    std::stop_token stop, const boost::json::object &payload, felis::bell::assistant ass,
    const felis::net::llm::StreamCallbacks &callbacks, felis::bell::error_code &code_out, std::string &error_out) {
  const std::string body = felis::str::sanitize_utf8(boost::json::serialize(payload));

  std::vector<felis::net::HttpHeader> headers{
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
  };

  // The assistant's authentication token was collected by the form and persisted
  // to the settings file, but never reached the wire: every request went out
  // unauthenticated, which only worked while a proxy injected its own key or the
  // endpoint needed none. Omit the header when the field is empty so keyless
  // local endpoints are not handed a bogus `Bearer `.
  if (!ass.authentication_token_.empty()) {
    headers.push_back({"Authorization", "Bearer " + ass.authentication_token_});
  }

  const bool chat = ass.protocol_ == felis::bell::api_protocol::chat_completions;

  auto result = felis::net::llm::stream_request(
      chat ? felis::net::llm::Protocol::ChatCompletions : felis::net::llm::Protocol::Responses,
      ass.api_url_ + (chat ? "/chat/completions" : "/responses"), ass.proxy_, headers, body, stop, 60'000, 320'000,
      callbacks);

  if (!result) {
    code_out = request_failure_code(result.error().kind);
    error_out = result.error().reason;
    felis::log::error("request failed: {}", error_out);
    return std::nullopt;
  }

  return std::move(result.value());
}

felis::bell::size_type felis::bell::client::append_message(size_type session_id, const shadowed_message &msg) {
  // Stamp the owning session: callers build a bare message (content + kind) and
  // rely on the client to key it. Without this the insert binds session_id 0 and
  // trips the FOREIGN KEY constraint.
  shadowed_message stamped = msg;
  stamped.session_id_ = session_id.get();
  return size_type(db_->insert_message(stamped));
}

std::vector<felis::bell::shadowed_message>
felis::bell::client::request_messages(size_type session_id, bool is_auto_compact) {
  // `compacted_at = 0` yields exactly "the last summary and everything after
  // it": summarization stamps every message it absorbed — including the previous
  // summary — with the id of the summary that consumed it, so only the newest
  // summary and the live tail survive with `compacted_at = 0`.
  //
  // The stored rows are already `shadowed_message`: that is the shape the
  // request builder consumes — the typed kind, the call identity of tool calls
  // and results, and the opaque reasoning handle, without the
  // write-through/`compacted_at` bookkeeping.
  std::vector<shadowed_message> messages = db_->all_not_compacted_messages(session_id);

  // The surviving summary absorbed the older history, but it is *inserted after*
  // the messages it summarised — so in id order it surfaces in the middle of (or
  // after) the preserved tail. The model must read it first, as the recap of the
  // past, then the verbatim tail in conversation order. Hoist every summary row
  // to the front (stable, so their own order and the tail's order are preserved).
  std::stable_partition(messages.begin(), messages.end(), [](const shadowed_message &m) {
    return m.kind_ == message_kind::inline_compact;
  });

  if (!is_auto_compact || messages.empty()) {
    return messages;
  }

  const std::optional<session> loaded = db_->get_session(session_id);
  if (!loaded.has_value()) {
    return messages;
  }

  // An auto compact request must not re-summarize the verbatim tail that
  // `auto_compact_preserve` reserves: those messages stay in the context after
  // compaction unchanged, so making the summarizer repeat them would duplicate
  // them once the summary is inserted. Walk from the newest message backwards,
  // charging each one's token estimate, and cut at the first message that no
  // longer fits the budget. What remains — `[0, cut)` — is the range to cover.
  const std::size_t preserve = loaded->auto_compact_preserve().get();

  std::size_t cut = messages.size();
  std::size_t reserved_tokens = 0;

  while (cut > 0) {
    const std::size_t cost = felis::str::estimate_tokens(messages[cut - 1].content_);
    if (reserved_tokens + cost > preserve) {
      break;
    }
    reserved_tokens += cost;
    --cut;
  }

  // Never starve the summarizer: if even the reserved tail exceeds the budget,
  // keep the oldest message in range rather than sending an empty request.
  if (cut == 0) {
    cut = 1;
  }

  // Do not cut a tool call away from the result answering it: a trailing
  // `function_call` left without its `function_call_output` makes the provider
  // reject the request, so pull the cut back over the unanswered call.
  while (cut > 1 && messages[cut - 1].kind_ == message_kind::assistant_tool_call) {
    --cut;
  }

  messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(cut), messages.end());
  return messages;
}

// Persist the user's turn here, on the caller's thread. The caller (the UI) then
// rebuilds the transcript immediately and sees the prompt at once; the worker
// only runs the model against the id we return.
felis::bell::size_type felis::bell::client::begin_turn(size_type session_id, const std::string &prompt_text) {
  return append_message(session_id, shadowed_message(prompt_text, message_kind::user_prompt));
}

felis::bell::prompt_result felis::bell::client::prompt_once(
    std::stop_token stop, size_type session_id, size_type user_message_id, const prompt_events &events,
    const std::function<std::vector<std::string>()> &take_steering) {

  static felis::synchronized<std::unordered_map<felis::bell::size_type::_Size_Type, std::mutex>> _session_id_locks;
  auto _session_lock = std::lock_guard<std::mutex>((*_session_id_locks.lock())[session_id.get()]);

  std::optional<session> loaded = db_->get_session(session_id);
  if (!loaded.has_value()) {
    felis::log::error("cannot prompt session {}: it does not exist", session_id.get());
    return {error_code::ERR_INVALID_SESSION_ID, "session does not exist"};
  }
  session sess = std::move(*loaded);
  assistant assistant_snapshot = assistant_state_->lock().data_->assistant_;

  // The user's turn is already persisted (`begin_turn`): its id is the per-turn
  // key that every request in this turn is measured against.

  tool_capabilities caps = sess.tools();

  bool idle = false;
  felis::bell::error_code failure = felis::bell::error_code::SUCCESS;
  std::string error;

  // Fold the eligible history into a summary, persist it, and mark the range it
  // absorbed as compacted. Returns false when there is nothing to summarize or
  // the summarization request fails; `failure`/`error` then carry why. The
  // summarizer's deltas are invisible context work: they never reach `on_text`,
  // and the single `on_status` below is the only user-visible sign it ran. A
  // blank summary is treated as a failed compaction: every absorbed message
  // would otherwise point at an empty bubble and vanish from the transcript, so
  // the history stays visible instead.
  auto compact = [&]() -> bool {
    const std::vector<shadowed_message> to_summarize = request_messages(session_id, true);
    if (to_summarize.empty()) {
      return false;
    }

    if (events.on_status)
      events.on_status("Compacting context\u2026");

    const auto summary_payload = build_compaction_request(
        "Summarize the conversation above so the work can continue without it. "
        "Keep every fact, decision, file path, command and open task; drop only "
        "small talk. Reply with the summary only.",
        to_summarize, sess.auto_compact_budget().get(), assistant_snapshot, sess.auto_compact_reasoning_effort());

    // The summarizer's deltas are invisible context work: they must never reach
    // `on_text`. Swallow the text, keep the stream alive.
    felis::net::llm::StreamCallbacks summary_cbs;
    summary_cbs.on_text = [](std::string_view) { return true; };

    std::string summary_error;
    felis::bell::error_code summary_failure = felis::bell::error_code::SUCCESS;
    std::optional<felis::net::llm::Result> summarized =
        stream_llm_request(stop, summary_payload, assistant_snapshot, summary_cbs, summary_failure, summary_error);

    if (!summarized.has_value()) {
      // A cancel is the user's choice, not a broken turn: keep its code (and no
      // message) so neither the tail nor the GUI reports it as a failure.
      if (summary_failure == felis::bell::error_code::ERR_CANCELLED) {
        failure = felis::bell::error_code::ERR_CANCELLED;
      } else {
        failure = felis::bell::error_code::ERR_COMPACTION_FAILED;
        error = "auto-compaction failed; stopped so the history is not lost: " + summary_error;
      }
      return false;
    }

    // A reasoning model can spend the whole output budget "thinking" and then
    // reply with a bare "\n\n" (or nothing), and some gateways do the same on
    // truncation. Folding real history into such a summary is catastrophic, so a
    // blank summary is a failed compaction and the history stays visible.
    if (summarized->text.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
      felis::log::error(
          "compaction produced a blank summary ({} output tokens); keeping history visible",
          summarized->usage.output_tokens);
      failure = felis::bell::error_code::ERR_COMPACTION_FAILED;
      error = "auto-compaction produced a blank summary; stopped so the history is not lost";
      return false;
    }

    const size_type summary_id =
        append_message(session_id, shadowed_message(summarized->text, message_kind::inline_compact));

    for (const shadowed_message &absorbed: to_summarize) {
      if (absorbed.id_ != 0) {
        db_->compact_message(size_type(absorbed.id_), size_type(summary_id));
      }
    }

    // Record the summarizer's own usage against the summary it produced, so
    // `context_usage` sees a paired `inline_compact` row and reports "unknown":
    // the pre-summary count must never be read as the new context size. One
    // request produces 0 to many message rows, so the measurement hangs on the
    // summary itself, not on a message.
    if (summarized->usage.reported) {
      db_->record_request_usage(
          summary_id, static_cast<std::uint32_t>(summarized->usage.input_tokens),
          static_cast<std::uint32_t>(summarized->usage.output_tokens));
    }

    return true;
  };

  while (!idle) {
    // Any failure here breaks out and falls through to the single return below;
    // see the function comment for why continuation is not an option.
    if (db_->exceeds_auto_compact_threshold(session_id)) {
      if (!compact()) {
        // `compact` names its own failure; if it left the code unset (nothing
        // eligible to fold) treat it as a compaction fault all the same.
        if (failure == felis::bell::error_code::SUCCESS) {
          failure = felis::bell::error_code::ERR_COMPACTION_FAILED;
          error = "auto-compaction found nothing to fold; stopped so the history is not lost";
        }
        break;
      }
    }

    const std::vector<shadowed_message> msgs = request_messages(session_id, false);

    const auto payload = build_turn_request(
        assistant_snapshot,
        build_custom_instructions(
            sess.custom_instructions(), assistant_snapshot.custom_instructions_, assistant_snapshot.memory_),
        msgs, sess.reasoning_effort(), caps, sess.response_budget().get());

    if (events.on_message_begin)
      events.on_message_begin();
    felis::net::llm::StreamCallbacks main_cbs;
    main_cbs.on_text = [&](std::string_view delta) -> bool {
      if (events.on_text)
        events.on_text(delta);
      return true;
    };
    main_cbs.on_reasoning = [&](std::string_view delta) {
      if (events.on_reasoning)
        events.on_reasoning(delta);
    };
    std::string serialized_payload = boost::json::serialize(payload);

    if constexpr (LOCAL_NOTEBOOK_DEBUG) {
      felis::log::info("{}\n{}", IDENTIFIER_LLM_PAYLOAD, serialized_payload);
    }

    std::string request_error;
    std::optional<felis::net::llm::Result> result =
        stream_llm_request(stop, payload, assistant_snapshot, main_cbs, failure, request_error);

    const felis::net::llm::Result *v = result ? &*result : nullptr;

    if (v == nullptr) {
      error = request_error;
      break;
    }

    // Measure this request against the turn's opening user message; see the
    // function comment for why it is a per-request row, not a message column.
    if (v != nullptr && v->usage.reported) {
      db_->record_request_usage(
          user_message_id, static_cast<std::uint32_t>(v->usage.input_tokens),
          static_cast<std::uint32_t>(v->usage.output_tokens));
    }

    // std::cout << std::format("Remote: {}\n", v.text);

    // Persist the reasoning item(s) this response produced; see the function
    // comment for why they come first and how each shape is normalised.
    if (v != nullptr) {
      boost::json::array reasoning_items;
      std::set<std::string> seen_reasoning_ids;

      for (const boost::json::object &item: v->response_items) {
        const boost::json::value *type = item.if_contains("type");
        if (type == nullptr || !type->is_string() || type->as_string() != "reasoning") {
          continue;
        }

        // Normalise to exactly the wire shape the Responses API accepts back:
        // {type, id, summary, content, encrypted_content, status}. Dropping any
        // other field the provider echoed avoids replaying unknown properties.
        boost::json::object normalized;
        normalized["type"] = "reasoning";

        if (const boost::json::value *id = item.if_contains("id"); id != nullptr) {
          if (id->is_string() && !seen_reasoning_ids.insert(std::string(id->as_string())).second) {
            continue;
          }
          normalized["id"] = *id;
        }
        if (const boost::json::value *summary = item.if_contains("summary"); summary != nullptr) {
          normalized["summary"] = *summary;
        }
        if (const boost::json::value *content = item.if_contains("content"); content != nullptr) {
          normalized["content"] = *content;
        }
        if (const boost::json::value *encrypted = item.if_contains("encrypted_content"); encrypted != nullptr) {
          normalized["encrypted_content"] = *encrypted;
        }
        if (const boost::json::value *status = item.if_contains("status"); status != nullptr) {
          normalized["status"] = *status;
        }

        reasoning_items.emplace_back(std::move(normalized));
      }

      // A chat-completions reasoning model (DeepSeek) streams its thinking through
      // `on_reasoning` instead of returning a `reasoning` item; see the function
      // comment.
      if (reasoning_items.empty() && !v->reasoning.empty()) {
        boost::json::object content;
        content["type"] = "reasoning_text";
        content["text"] = v->reasoning;

        boost::json::object synthetic;
        synthetic["type"] = "reasoning";
        synthetic["content"] = boost::json::array{std::move(content)};
        reasoning_items.emplace_back(std::move(synthetic));
      }

      if (!reasoning_items.empty()) {
        boost::json::object envelope;
        envelope["reasoning_items"] = std::move(reasoning_items);
        shadowed_message reasoning_row(std::string(), message_kind::assistant_reasoning_payload);
        reasoning_row.reasoning_payload_ = boost::json::serialize(envelope);
        append_message(session_id, reasoning_row);
      }
    }

    // The proxy rejects empty assistant text items, so only persist a reply
    // that actually carries something to replay.
    const std::string &reply_text = v->text;
    if (!reply_text.empty()) {
      append_message(session_id, shadowed_message(reply_text, message_kind::assistant_output));
    }

    if (v != nullptr && !v->tool_calls.empty()) {
      for (const auto &call: v->tool_calls) {
        append_message(
            session_id, shadowed_message(call.arguments, message_kind::assistant_tool_call, {call.id, call.name}));
      }

      // The calls are stored but not yet run: tell the caller now so the strips
      // are on screen while each tool blocks, rather than surfacing only when
      // the next model call's `on_message_begin` rebuilds the transcript.
      if (events.on_rows_appended)
        events.on_rows_appended();

      for (const auto &call: v->tool_calls) {
        if (events.on_status)
          events.on_status("Running " + call.name);
        std::string tool_output = felis::bell::execute_local_tool(
            call, sess.shared_directories(), assistant_snapshot.proxy_, caps,
            assistant_snapshot.search_endpoint_.empty() ? std::nullopt
                                                        : std::optional(assistant_snapshot.search_endpoint_),
            stop);

        append_message(session_id, shadowed_message(tool_output, message_kind::tool_result, {call.id, call.name}));

        // Surface each result as it lands, so the strip replaces the running
        // placeholder while the remaining calls (if any) continue.
        if (events.on_rows_appended)
          events.on_rows_appended();
      }
    }

    // A cancellation that landed while a tool was running ends the turn here.
    if (stop.stop_requested()) {
      failure = felis::bell::error_code::ERR_CANCELLED;
      break;
    }

    // Prompts typed while this request ran are taken now, after its rows.
    std::vector<std::string> steered_prompts;
    if (take_steering) {
      steered_prompts = take_steering();
    }

    if (!steered_prompts.empty()) {
      for (const std::string &prompt: steered_prompts) {
        const size_type steer_id = append_message(session_id, shadowed_message(prompt, message_kind::user_prompt));
        if (events.on_steered)
          events.on_steered(steer_id);
      }

      // Answer the queued prompts in this same turn instead of parking them
      // until the user sends again.
      idle = false;
    } else {
      idle = v->tool_calls.empty();
    }
  }

  return {failure, std::move(error)};
}

// The retry loop that wraps one attempt. It catches an exception escaping an
// attempt as an unknown exit (`ERR_INTERNAL`) so it never unwinds past the
// caller, then re-attempts only when the policy and the stored-row count allow
// it. `begin_turn` is never re-run: the user's prompt is already persisted, so
// an attempt resumes from exactly where the last one stopped.
felis::bell::prompt_result felis::bell::client::prompt(
    std::stop_token stop, size_type session_id, size_type user_message_id, const prompt_events &events,
    const std::function<std::vector<std::string>()> &take_steering, retry_policy policy) {
  for (int attempt = 0;; ++attempt) {
    const std::size_t rows_before = db_->message_count(session_id.get());

    prompt_result result;
    try {
      result = prompt_once(stop, session_id, user_message_id, events, take_steering);
    } catch (const std::exception &escaped) {
      if (stop.stop_requested()) {
        result = {error_code::ERR_CANCELLED, {}};
      } else {
        felis::log::error("prompt session {} left by exception: {}", session_id.get(), escaped.what());
        result = {error_code::ERR_INTERNAL, escaped.what()};
      }
    } catch (...) {
      if (stop.stop_requested()) {
        result = {error_code::ERR_CANCELLED, {}};
      } else {
        felis::log::error("prompt session {} left by an unknown exception", session_id.get());
        result = {error_code::ERR_INTERNAL, "the turn stopped for an unknown reason"};
      }
    }

    if (result.ok() || stop.stop_requested() || !policy.retries(result.code)
        || attempt >= policy.max_retries) {
      if (!result.ok() && result.code != error_code::ERR_CANCELLED) {
        felis::log::error("prompt session {} failed: {}", session_id.get(), result.error);
      }
      return result;
    }

    // An unknown exit is retried only when the attempt stored nothing: a partial
    // turn already in the transcript must not be replayed into a duplicate.
    if (result.code == error_code::ERR_INTERNAL && db_->message_count(session_id.get()) != rows_before) {
      felis::log::error("prompt session {} failed after storing rows: {}", session_id.get(), result.error);
      return result;
    }

    felis::log::info(
        "turn attempt {}/{} failed ({}); retrying in {} ms", attempt + 1, policy.max_retries + 1,
        to_string(result.code), policy.interval.count());

    // Drop the torn bubble and reload the transcript before the new attempt
    // streams, so its first delta does not append to the dead one.
    if (events.on_status) {
      events.on_status("Reconnecting\u2026");
    }
    if (events.on_message_begin) {
      events.on_message_begin();
    }

    if (!sleep_interruptibly(stop, policy.interval)) {
      return {error_code::ERR_CANCELLED, {}};
    }
  }
}

felis::bell::prompt_result felis::bell::client::update_memory(std::stop_token stop, const prompt_events &events) {
  // Snapshot the assistant once: the request is built from a consistent copy
  // even if the settings window edits the assistant while this runs.
  const assistant assistant_snapshot = assistant_state_->lock().data_->assistant_;

  const std::vector<session_summary> summaries =
      db_->session_summaries(assistant_snapshot.memory_budget_for_session_compact_);
  const std::vector<shadowed_message> recent =
      db_->messages_across_sessions(assistant_snapshot.memory_budget_across_messages_);

  // Nothing to merge is not an error: the first press on a fresh assistant.
  if (summaries.empty() && recent.empty()) {
    felis::log::info("memory update skipped: no summaries and no recent messages");
    return {error_code::ERR_NOTHING_TO_UPDATE, "no new conversation to merge into memory yet"};
  }

  if (events.on_status)
    events.on_status("Updating memory\u2026");

  const boost::json::object payload = build_memory_request(
      assistant_snapshot.memory_, summaries, recent, assistant_snapshot.memory_budget_, assistant_snapshot);

  // The memory update is invisible context work: its deltas must never reach the
  // transcript. Swallow the text and keep the stream alive.
  felis::net::llm::StreamCallbacks callbacks;
  callbacks.on_text = [](std::string_view) { return true; };

  std::string request_error;
  felis::bell::error_code failure = felis::bell::error_code::SUCCESS;
  const std::optional<felis::net::llm::Result> merged =
      stream_llm_request(stop, payload, assistant_snapshot, callbacks, failure, request_error);

  if (!merged.has_value()) {
    felis::log::error("memory update request failed: {}", request_error);
    return {failure, request_error};
  }

  // Same guard as compaction: a reasoning model can spend the whole output budget
  // thinking and reply with a bare "\n\n". Overwriting the memory with that
  // would silently erase it.
  if (merged->text.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
    felis::log::error(
        "memory update produced a blank reply ({} output tokens); memory left unchanged", merged->usage.output_tokens);
    return {error_code::ERR_BLANK_REPLY, "the model returned no memory text"};
  }

  assistant updated = assistant_snapshot;
  updated.memory_ = merged->text;
  set_assistant(updated);

  return {};
}

felis::bell::client::client(store store, const std::string &uuid)
    : store_(std::move(store)), assistant_uuid_(uuid), assistant_state_(open_state()),
      db_(std::make_unique<database>(store_.assistant_db(uuid).string())) {
  felis::log::info("opened assistant {} at {}", uuid, store_.assistant_dir(uuid).string());
}

std::unique_ptr<felis::synchronized<felis::bell::assistant_state>> felis::bell::client::open_state() {
  const std::string &uuid = assistant_uuid_;

  assistant loaded = store_.load_assistant(uuid);

  auto filesystem_lock = std::make_unique<felis::legacy::instance_lock>();
  std::string reason;
  if (!filesystem_lock->acquire(store_.assistant_lock(uuid), reason)) {
    // A refusal is ordinary: another instance may legitimately have this
    // assistant open. Construction cannot leave a half-open client, so it unwinds
    // instead; the reason (the holding process, or a filesystem error) is logged
    // for the caller, which only needs the code to pick a sentence.
    felis::log::error("cannot open assistant {}: {}", uuid, reason);
    throw open_error(error_code::ERR_ASSISTANT_LOCK_FAILED);
  }

  open_log(uuid);

  return std::make_unique<felis::synchronized<assistant_state>>(uuid, std::move(loaded), std::move(filesystem_lock));
}
