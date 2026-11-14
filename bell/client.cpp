#include "common.hpp"
#include "store.hpp"
#include "tools.hpp"
#include "felis/log.hpp"
#include "felis/str.hpp"
#include "message.hpp"
#include <iostream>
#include <algorithm>
#include <set>
#include <string_view>
#include <cstdio>
#include <ctime>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <felis/log.hpp>
#include <boost/json.hpp>

#include <felis/net/llm.hpp>

#include "client.hpp"
#include "application.hpp"




[[nodiscard]] boost::json::array build_input(
  const std::vector<felis::bell::shadowed_message>& msgs
) {
  using namespace felis::bell;

  boost::json::array input;

  // Reasoning items are replayed by `id`; a later duplicate overwrites (here:
  // is skipped) so the same reasoning item is never sent twice in one request.
  std::set<std::string> replayed_reasoning_ids;

  for (const shadowed_message& msg: msgs) {
    switch (msg.kind_) {
      case message_kind::user_prompt: {
        boost::json::object entry;
        entry["role"] = "user";
        entry["content"] = msg.content_;
        input.emplace_back(std::move(entry));
        break;
      }

      case message_kind::assistant_output: {
        // The assistant's own text is an ordinary message item; the calls it
        // requested become sibling `function_call` items, matched by `call_id`.
        boost::json::object entry;
        entry["role"] = "assistant";
        entry["content"] = msg.content_;
        input.emplace_back(std::move(entry));
        break;
      }
      case message_kind::assistant_tool_call: {
        boost::json::object entry;
        entry["type"]      = "function_call";
        entry["call_id"]   = msg.tool_call_.value().tool_call_id_;
        entry["name"]      = msg.tool_call_.value().tool_name_;
        entry["arguments"] = msg.content_;   // raw JSON string from ToolCall::arguments
        input.emplace_back(std::move(entry));
        break;
      }
      case message_kind::tool_result: {
        boost::json::object entry;
        entry["type"] = "function_call_output";
        entry["call_id"] = msg.tool_call_.value().tool_call_id_;
        entry["output"] = msg.content_;
        input.emplace_back(std::move(entry));
        break;
      }

      case message_kind::inline_compact: {
        // A folded summary of the compacted history. It sits at the head of the
        // conversation (right after the instructions, before the live tail) and
        // is replayed as an assistant turn so the model reads it as prior
        // context rather than as a fresh user request.
        boost::json::object entry;
        entry["role"] = "assistant";
        entry["content"] = msg.content_;
        input.emplace_back(std::move(entry));
        break;
      }

      case message_kind::assistant_reasoning_payload: {
        // The reasoning item(s) a GPT/o-series response produced, carrying the
        // opaque `encrypted_content` that lets the model resume its own
        // chain-of-thought. Replayed verbatim as `type:"reasoning"` input items;
        // because the row was inserted *before* the output/`function_call` items
        // it belongs to, they land in the right position. A deepseek-style model
        // stores no payload, so there is nothing to emit.
        if (!msg.reasoning_payload_.has_value() || msg.reasoning_payload_->empty()) {
          break;
        }
        boost::system::error_code ec;
        const boost::json::value parsed = boost::json::parse(*msg.reasoning_payload_, ec);
        if (ec || !parsed.is_object()) {
          break;
        }
        const boost::json::value* items =
          parsed.as_object().if_contains("reasoning_items");
        if (items == nullptr || !items->is_array()) {
          break;
        }
        for (const boost::json::value& item : items->as_array()) {
          if (!item.is_object()) {
            continue;
          }
          const boost::json::object& object = item.as_object();
          if (const boost::json::value* id = object.if_contains("id");
              id != nullptr && id->is_string()) {
            if (!replayed_reasoning_ids.insert(std::string(id->as_string())).second) {
              continue;
            }
          }
          input.emplace_back(boost::json::value(object));
        }
        break;
      }

      default: {
        // `undefined` carries nothing to send here.
        break;
      }
    }
  }

  return input;
}

[[nodiscard]] boost::json::object build_responses_request(
  const std::string& custom_instructions,
  const std::vector<felis::bell::shadowed_message>& msgs,
  const std::string& model,
  const std::string& reasoning_effort,
  const felis::bell::tool_capabilities capabilities
) {
  boost::json::object payload;
  payload["model"] = model;
  payload["input"] = build_input(msgs);
  payload["stream"] = true;

  payload["tools"] = responses_tools(capabilities);

  // The session's custom instructions are the top-level `instructions` field of a
  // Responses request (Zed does the same: `responses.rs` `instructions:
  // Option<String>`), not a message in `input`. Omit it when empty so the proxy
  // does not receive an empty system prompt.
  if (!custom_instructions.empty()) {
    payload["instructions"] = custom_instructions;
  }

  // Do not ask the provider to store the response: this client keeps the whole
  // transcript itself and replays it, so `store:false` mirrors Zed and avoids
  // depending on server-side conversation state.
  payload["store"] = false;

  // Request reasoning at the session's chosen level. An empty string or "none"
  // means "do not request reasoning" (the user turned it off, or the model does
  // not reason), so the field is omitted rather than sent as "none" — some
  // gateways reject an explicit "none" effort.
  const bool reasoning_on = !reasoning_effort.empty() && reasoning_effort != "none";
  if (reasoning_on) {
    boost::json::object reasoning;
    reasoning["effort"] = reasoning_effort;
    payload["reasoning"] = std::move(reasoning);
  }

  // Ask for the opaque `encrypted_content` half of every reasoning item so it can
  // be stored and replayed. Needed whenever reasoning is on, and also whenever the
  // input already carries stored reasoning items (GPT/o-series): without it the
  // provider strips `encrypted_content` from the round-trip and the next request's
  // replayed reasoning item is rejected. Mirrors Zed's `include` decision.
  bool has_stored_reasoning = false;
  for (const felis::bell::shadowed_message& msg : msgs) {
    if (msg.kind_ == felis::bell::message_kind::assistant_reasoning_payload
        && msg.reasoning_payload_.has_value()
        && !msg.reasoning_payload_->empty()) {
      has_stored_reasoning = true;
      break;
    }
  }
  if (reasoning_on || has_stored_reasoning) {
    boost::json::array include;
    include.emplace_back("reasoning.encrypted_content");
    payload["include"] = std::move(include);
  }

  return payload;
}

// Builds the single request that folds the eligible history into a summary. It
// carries no tools (the model must only summarize, never act) and a dedicated
// instruction, and is capped by the session's compaction output budget.
[[nodiscard]] boost::json::object build_compaction_request(
  const std::string& summarizer_instructions,
  const std::vector<felis::bell::shadowed_message>& msgs,
  std::size_t max_output_tokens,
  felis::bell::assistant ass,
  const std::string& auto_compact_reasoning_effort
) {
  boost::json::object payload;
  payload["model"] = ass.model_;

  // Build the transcript to summarise, then append an explicit closing USER turn
  // that asks for the summary. This is essential, not cosmetic: the eligible
  // range almost always ends on a `function_call_output` (a tool result) — or an
  // assistant turn — with no user message after it. A tool-result-tipped input
  // makes the model believe the task is still in progress, so it *resumes the
  // work* (emitting task-continuation text, or a bare "\n\n" once its reasoning
  // budget is spent) instead of summarising. The `instructions` field alone does
  // not override that continuation reflex. A trailing user instruction does:
  // verified against the proxy, it turns a blank/task-continuation reply into a
  // clean "## Conversation Summary".
  // The summariser only needs the readable transcript, so drop the stored
  // reasoning-item rows: replaying `encrypted_content` into a summarisation turn
  // (usually run at `effort:"none"`) is pointless and some gateways reject a
  // reasoning input item when reasoning is disabled.
  std::vector<felis::bell::shadowed_message> readable;
  readable.reserve(msgs.size());
  for (const felis::bell::shadowed_message& msg : msgs) {
    if (msg.kind_ != felis::bell::message_kind::assistant_reasoning_payload) {
      readable.push_back(msg);
    }
  }

  boost::json::array summary_input = build_input(readable);
  {
    boost::json::object closing_turn;
    closing_turn["role"] = "user";
    closing_turn["content"] =
        "Summarize the entire conversation above as a running summary for "
        "continuation. Output only the summary text \u2014 no preamble, no tool "
        "calls, no questions.";
    summary_input.emplace_back(std::move(closing_turn));
  }
  payload["input"] = std::move(summary_input);
  payload["stream"] = true;
  payload["instructions"] = summarizer_instructions;

  if (max_output_tokens > 0) {
    payload["max_output_tokens"] = static_cast<std::int64_t>(max_output_tokens);
  }

  // The summariser must not spend its output budget on hidden reasoning: a
  // reasoning model that thinks through the whole budget replies with blank (or
  // truncated) text, and `compact()`'s blank-summary guard then throws the whole
  // compaction away, leaving nothing to render inline. Honour the session's
  // dedicated summarisation reasoning level (`auto_compact_reasoning_effort`, default
  // "none") so summarisation emits visible text instead of only thinking. This is
  // a SESSION setting, not the assistant's: chat turns are unaffected, and the
  // inline summary always runs on the same model as the chat.
  if (!auto_compact_reasoning_effort.empty()) {
    boost::json::object reasoning;
    reasoning["effort"] = auto_compact_reasoning_effort;
    payload["reasoning"] = std::move(reasoning);
  }

  return payload;
}

// One streaming POST against the configured Responses endpoint. The decoded
// deltas drive `callbacks`; on success the finished turn is returned, otherwise
// `std::nullopt` (with the reason copied into `error_out`).
[[nodiscard]] std::optional<felis::net::llm::Result> stream_llm_request(
  std::stop_token stop,
  const boost::json::object& payload,
  felis::bell::assistant ass,
  const felis::net::llm::StreamCallbacks& callbacks,
  std::string& error_out
) {
  const std::string body = felis::str::sanitize_utf8(boost::json::serialize(payload));

  std::vector<felis::net::HttpHeader> headers{
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
  };

  auto result = felis::net::llm::stream_request(
    felis::net::llm::Protocol::Responses,
    ass.api_url_ + "/responses",
    ass.proxy_,
    headers,
    body,
    stop,
    60'000,
    320'000,
    callbacks);

  if (!result) {
    error_out = result.error().reason;
    felis::log::error("request failed: {}", error_out);
    return std::nullopt;
  }

  return std::move(result.value());
}

felis::bell::size_type felis::bell::client::append_message(
    size_type session_id, const shadowed_message& msg) {
  // Stamp the owning session: callers build a bare message (content + kind) and
  // rely on the client to key it. Without this the insert binds session_id 0 and
  // trips the FOREIGN KEY constraint.
  shadowed_message stamped = msg;
  stamped.session_id_ = session_id.get();
  return size_type(db_->insert_message(stamped));
}

std::vector<felis::bell::shadowed_message> felis::bell::client::request_messages(
    size_type session_id, bool is_auto_compact) {
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
  std::stable_partition(messages.begin(), messages.end(),
    [](const shadowed_message& m) { return m.kind_ == message_kind::inline_compact; });

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

felis::bell::size_type felis::bell::client::begin_turn(
    size_type session_id, const std::string& prompt_text) {
  // Persist the user's turn here, on the caller's thread. The caller (the UI) then
  // rebuilds the transcript immediately and sees the prompt at once; the worker
  // only runs the model against the id we return.
  return append_message(
      session_id, shadowed_message(prompt_text, message_kind::user_prompt));
}

void felis::bell::client::prompt(
    std::stop_token stop,
    size_type session_id,
    size_type user_message_id,
    const prompt_events& events) {

  std::optional<session> loaded = db_->get_session(session_id);
  if (!loaded.has_value()) {
    felis::log::error("cannot prompt session {}: it does not exist", session_id.get());
    if (events.on_done) events.on_done(false, "session does not exist");
    return;
  }
  session sess = std::move(*loaded);
  assistant assistant_snapshot = assistant_state_->lock().data_->assistant_;

  // The user's turn is already persisted (`begin_turn`): its id is the per-turn
  // key that every request in this turn is measured against.

  tool_capabilities caps = sess.tools();
  // Fold the eligible history into a summary, persist it, and mark the range it
  // absorbed as compacted. Returns false when there is nothing to summarize or
  // the summarization request fails.
  auto compact = [&]() -> bool {
    const std::vector<shadowed_message> to_summarize = request_messages(session_id, true);
    if (to_summarize.empty()) {
      return false;
    }

    if (events.on_status) events.on_status("Compacting context\u2026");

    const auto summary_payload = build_compaction_request(
      "Summarize the conversation above so the work can continue without it. "
      "Keep every fact, decision, file path, command and open task; drop only "
      "small talk. Reply with the summary only.",
      to_summarize,
      sess.auto_compact_budget().get(),
      assistant_snapshot,
      sess.auto_compact_reasoning_effort()
    );

    // The summarizer's deltas are invisible context work: they must never reach
    // `on_text`. Swallow the text, keep the stream alive, and let the single
    // `on_status` above be the only user-visible sign that compaction ran.
    felis::net::llm::StreamCallbacks summary_cbs;
    summary_cbs.on_text = [](std::string_view) { return true; };

    std::string summary_error;
    std::optional<felis::net::llm::Result> summarized = stream_llm_request(
      stop, summary_payload, assistant_snapshot, summary_cbs, summary_error);

    if (!summarized.has_value()) {
      return false;
    }

    // A reasoning model can spend the whole output budget "thinking" and then
    // reply with a bare "\n\n" (or nothing), and some gateways do the same on
    // truncation. Folding real history into such a summary is catastrophic: every
    // absorbed message gets `compacted_at` pointing at an empty bubble and
    // disappears from the transcript with nothing to replace it. Treat a blank
    // summary as a failed compaction and leave the history visible instead.
    if (summarized->text.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
      felis::log::error(
        "compaction produced a blank summary ({} output tokens); keeping history visible",
        summarized->usage.output_tokens
      );
      return false;
    }

    const size_type summary_id =
      append_message(session_id, shadowed_message(summarized->text, message_kind::inline_compact));

    for (const shadowed_message& absorbed : to_summarize) {
      if (absorbed.id_ != 0) {
        db_->compact_message(size_type(absorbed.id_), size_type(summary_id));
      }
    }

    // Record the summarizer's own usage against the summary it produced, so
    // `context_usage` sees a paired `inline_compact` row and reports "unknown":
    // the pre-summary count must never be read as the new context size.
    if (summarized->usage.reported) {
      db_->record_request_usage(
        summary_id,
        static_cast<std::uint32_t>(summarized->usage.input_tokens),
        static_cast<std::uint32_t>(summarized->usage.output_tokens));
    }

    return true;
  };

  bool idle = false;
  bool ok = true;
  std::string error;

  while (!idle) {
    // Compaction runs at the head of every tool-loop iteration (it may fire
    // mid-turn), mirroring Zed's top-of-loop check. If it fails (blank summary,
    // request error, nothing absorbed) we must NOT continue: the alternative is
    // building a request from a transcript that is already over the threshold,
    // which either fails the same way again or silently drops history. Stop the
    // turn instead, and surface it through `on_done` below — throwing here would
    // unwind past `on_done` and leave the caller's turn stuck as "in progress".
    if (db_->exceeds_auto_compact_threshold(session_id)) {
      if (!compact()) {
        ok = false;
        error = "auto-compaction failed; stopped so the history is not lost";
        break;
      }
    }

    const std::vector<shadowed_message> msgs = request_messages(session_id, false);

    const auto payload = build_responses_request(
      sess.custom_instructions(),
      msgs,
      assistant_snapshot.model_,
      sess.reasoning_effort(),
      caps);

    // Signal the start of this assistant message *before* any of its deltas. A
    // single turn runs one model call per tool-loop iteration, and each call
    // streams its own assistant message; without this boundary the UI would
    // concatenate one message's text directly onto the next (across the tool
    // calls between them), merging separate replies into a single paragraph
    // until the turn ended and the DB round-trip split them apart again.
    if (events.on_message_begin) events.on_message_begin();

    // Forward the visible text and hidden reasoning to the caller as they
    // stream. The stop token is observed inside `stream_llm_request`, so `on_text`
    // never has to abort on cancellation here.
    felis::net::llm::StreamCallbacks main_cbs;
    main_cbs.on_text = [&](std::string_view delta) -> bool {
      if (events.on_text) events.on_text(delta);
      return true;
    };
    main_cbs.on_reasoning = [&](std::string_view delta) {
      if (events.on_reasoning) events.on_reasoning(delta);
    };

    std::string request_error;
    std::optional<felis::net::llm::Result> result = stream_llm_request(
      stop, payload, assistant_snapshot, main_cbs, request_error);

    if (!result.has_value()) {
      ok = false;
      error = request_error;
      break;
    }

    const felis::net::llm::Result& v = *result;

    // Measure this request against the turn's opening user message; reported
    // usage is exactly what the auto-compact predicate reads next round.
    if (v.usage.reported) {
      db_->record_request_usage(
        user_message_id,
        static_cast<std::uint32_t>(v.usage.input_tokens),
        static_cast<std::uint32_t>(v.usage.output_tokens));
    }

    // std::cout << std::format("Remote: {}\n", v.text);

    // Persist the reasoning item(s) this response produced (GPT/o-series
    // `encrypted_content`) so a later request can replay them and the model can
    // resume its chain-of-thought. Inserted *first*, ahead of this turn's
    // assistant text and tool calls, so the replayed `reasoning` item lands before
    // the output/`function_call` items it belongs to. deepseek-style models emit
    // no reasoning items and store no payload, so this row is skipped for them.
    {
      boost::json::array reasoning_items;
      std::set<std::string> seen_reasoning_ids;

      for (const boost::json::object& item : v.response_items) {
        const boost::json::value* type = item.if_contains("type");
        if (type == nullptr || !type->is_string() || type->as_string() != "reasoning") {
          continue;
        }

        // Normalise to exactly the wire shape the Responses API accepts back:
        // {type, id, summary, content, encrypted_content, status}. Dropping any
        // other field the provider echoed avoids replaying unknown properties.
        boost::json::object normalized;
        normalized["type"] = "reasoning";

        if (const boost::json::value* id = item.if_contains("id"); id != nullptr) {
          if (id->is_string()
              && !seen_reasoning_ids.insert(std::string(id->as_string())).second) {
            continue;
          }
          normalized["id"] = *id;
        }
        if (const boost::json::value* summary = item.if_contains("summary"); summary != nullptr) {
          normalized["summary"] = *summary;
        }
        if (const boost::json::value* content = item.if_contains("content"); content != nullptr) {
          normalized["content"] = *content;
        }
        if (const boost::json::value* encrypted = item.if_contains("encrypted_content");
            encrypted != nullptr) {
          normalized["encrypted_content"] = *encrypted;
        }
        if (const boost::json::value* status = item.if_contains("status"); status != nullptr) {
          normalized["status"] = *status;
        }

        reasoning_items.emplace_back(std::move(normalized));
      }

      if (!reasoning_items.empty()) {
        boost::json::object envelope;
        envelope["reasoning_items"] = std::move(reasoning_items);
        shadowed_message reasoning_row(
          std::string(), message_kind::assistant_reasoning_payload);
        reasoning_row.reasoning_payload_ = boost::json::serialize(envelope);
        append_message(session_id, reasoning_row);
      }
    }

    // The proxy rejects empty assistant text items, so only persist a reply
    // that actually carries something to replay.
    if (!v.text.empty()) {
      append_message(session_id, shadowed_message(v.text, message_kind::assistant_output));
    }

    if (!v.tool_calls.empty()) {
      for (const auto& call : v.tool_calls) {
        append_message(
          session_id, shadowed_message(call.arguments, message_kind::assistant_tool_call, {call.id, call.name}));
      }

      for (const auto& call : v.tool_calls) {
        if (events.on_status) events.on_status("Running " + call.name);
        std::string tool_output = felis::bell::execute_local_tool(
          call,
          sess.shared_directories(),
          assistant_snapshot.proxy_,
          caps);

        append_message(
          session_id, shadowed_message(tool_output, message_kind::tool_result, {call.id, call.name}));
      }

      idle = false;
    } else {
      idle = true;
    }
  }

  if (events.on_done) {
    events.on_done(ok, error);
  }
}

felis::bell::error_code felis::bell::client::open_assistant() {
  const auto& uuid = assistant_uuid_;

  static std::mutex function_mutex;
  std::lock_guard function_lock(function_mutex);

  assistant assistant = store_.load_assistant(uuid);
  const std::string requested = uuid;

  bool already_held = false;
  if (assistant_state_ != nullptr) {
    auto state = assistant_state_->lock();
    already_held = state->filesystem_lock_ != nullptr && state->uuid_ == uuid;
  }

  std::unique_ptr<felis::legacy::instance_lock> filesystem_lock;
  if (!already_held) {
    filesystem_lock = std::make_unique<felis::legacy::instance_lock>();
    std::string error;
    if (!filesystem_lock->acquire(store_.assistant_lock(uuid), error)) {
      felis::raise("cannot open assistant {}: {}", uuid, error);
      // return std::unexpected(error_code::ERR_ASSISTANT_HAS_NOT_LOADED_YET);
    }
  } else {
    filesystem_lock = std::move(assistant_state_->lock()->filesystem_lock_);
  }

  // stop_worker();
  assistant_state_ = std::make_unique<felis::synchronized<assistant_state>>(
      requested, std::move(assistant), std::move(filesystem_lock));

  open_log(requested);
  felis::log::info("opened assistant {} at {}", requested, store_.assistant_dir(requested).string());

  db_ = std::make_unique<database>(store_.assistant_db(requested).string());

  return error_code::success;
}
