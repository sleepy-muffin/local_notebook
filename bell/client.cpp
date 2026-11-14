#include "common.hpp"
#include "store.hpp"
#include "tools.hpp"
#include "felis/log.hpp"
#include "felis/str.hpp"
#include "message.hpp"
#include <iostream>
#include <algorithm>
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

      default: {
        // `undefined` and `assistant_reasoning` carry nothing to send here.
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
  //   if (request.max_output_tokens > 0) {
  //     payload["max_output_tokens"] = request.max_output_tokens;
  //   }
  //
  //   if (!request.reasoning_effort.empty()) {
  //     boost::json::object reasoning;
  //     reasoning["effort"] = request.reasoning_effort;
  //     payload["reasoning"] = std::move(reasoning);
  //   }

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
  boost::json::array summary_input = build_input(msgs);
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
      caps);

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
