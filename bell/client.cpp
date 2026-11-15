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




// A stored reasoning payload is only replayable when it carries the provider's
// opaque `encrypted_content`. That blob is the only part a strict gateway can
// decode back into the chain of thought, so an item without it is not worth the
// input tokens: the Responses protocol has nothing to match it against. Models
// that answer with a readable `summary` only (DeepSeek under this protocol) never
// produce one, so their rows are kept for display but not replayed.
//
// Measured against the current gateway (2026-11-15): it accepts any reasoning
// item -- empty blob, garbage blob, summary-only, even out of order -- with HTTP
// 200 instead of rejecting it, and DeepSeek charges zero tokens for it. So this
// is a correctness/token-hygiene guard, not a crash guard.
[[nodiscard]] bool payload_has_replayable_reasoning(const std::string& payload) {
  if (payload.empty()) {
    return false;
  }

  boost::system::error_code ec;
  const boost::json::value parsed = boost::json::parse(payload, ec);
  if (ec || !parsed.is_object()) {
    return false;
  }

  const boost::json::value* items = parsed.as_object().if_contains("reasoning_items");
  if (items == nullptr || !items->is_array()) {
    return false;
  }

  for (const boost::json::value& item : items->as_array()) {
    if (!item.is_object()) {
      continue;
    }
    const boost::json::value* encrypted =
      item.as_object().if_contains("encrypted_content");
    if (encrypted != nullptr && encrypted->is_string() && !encrypted->as_string().empty()) {
      return true;
    }
  }

  return false;
}

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
        // it belongs to, they land in the right position. A summary-only payload
        // (DeepSeek) is stored for display but must not be sent back.
        if (!msg.reasoning_payload_.has_value()
            || !payload_has_replayable_reasoning(*msg.reasoning_payload_)) {
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
          const boost::json::value* encrypted = object.if_contains("encrypted_content");
          if (encrypted == nullptr || !encrypted->is_string()
              || encrypted->as_string().empty()) {
            continue;
          }
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
  const felis::bell::tool_capabilities capabilities,
  std::size_t max_output_tokens
) {
  boost::json::object payload;
  payload["model"] = model;
  payload["input"] = build_input(msgs);
  payload["stream"] = true;

  // The session's response budget caps how many tokens the model may produce for
  // this request. Without it the provider falls back to its own default, which
  // silently ignores the number the session editor shows. 0 disables the cap.
  if (max_output_tokens > 0) {
    payload["max_output_tokens"] = static_cast<std::int64_t>(max_output_tokens);
  }

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
        && payload_has_replayable_reasoning(*msg.reasoning_payload_)) {
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

// Builds the single request that merges the assistant's memory. It carries no
// tools (the model must only merge, never act), states the merge rules in
// `instructions`, and ends with an explicit user turn asking for the result --
// without that trailing turn a transcript tipped by an assistant reply makes the
// model continue the conversation instead of writing the memory.
[[nodiscard]] boost::json::object build_memory_request(
  const std::string& current_memory,
  const std::vector<felis::bell::session_summary>& summaries,
  const std::vector<felis::bell::shadowed_message>& recent_messages,
  std::size_t max_output_tokens,
  const felis::bell::assistant& ass
) {
  boost::json::array input;

  // The existing memory and each session's own summary form one context block:
  // they are all background the model already knows, not turns to answer.
  std::string context;
  if (!current_memory.empty()) {
    context += "## Current memory\n";
    context += current_memory;
    context += "\n\n";
  }
  for (const felis::bell::session_summary& summary : summaries) {
    context += "## Session: " + summary.session_title_ + "\n";
    context += summary.text_;
    context += "\n\n";
  }
  if (!context.empty()) {
    boost::json::object entry;
    entry["role"] = "user";
    entry["content"] = std::move(context);
    input.emplace_back(std::move(entry));
  }

  // The newest conversation rows across sessions, in conversation order, so the
  // model reads what happened after those summaries.
  for (const felis::bell::shadowed_message& msg : recent_messages) {
    boost::json::object entry;
    entry["role"] = msg.kind_ == felis::bell::message_kind::user_prompt
        ? "user"
        : "assistant";
    entry["content"] = msg.content_;
    input.emplace_back(std::move(entry));
  }

  {
    boost::json::object closing_turn;
    closing_turn["role"] = "user";
    closing_turn["content"] =
        "Write the updated memory now: one compact Markdown note that keeps every "
        "standing fact, decision, preference, open task and file path, drops small "
        "talk and anything already stale, and merges duplicates. Output only the "
        "memory text \u2014 no preamble, no tool calls, no questions.";
    input.emplace_back(std::move(closing_turn));
  }

  boost::json::object payload;
  payload["model"] = ass.model_;
  payload["input"] = std::move(input);
  payload["stream"] = true;
  payload["store"] = false;
  payload["instructions"] =
      "You maintain the assistant's long-term memory: a short note carried into "
      "every future session. You are given the memory as it stands, each session's "
      "own summary, and the newest messages across sessions. Rewrite the memory so "
      "it absorbs the new material without growing without bound; omit what is no "
      "longer relevant. Never invent facts that are not in the material above.";

  if (max_output_tokens > 0) {
    payload["max_output_tokens"] = static_cast<std::int64_t>(max_output_tokens);
  }

  // An empty value or "none" means "do not request reasoning": the field is
  // omitted rather than sent as "none", which some gateways reject (mirrors
  // `build_responses_request`).
  if (!ass.reasoning_effort_.empty() && ass.reasoning_effort_ != "none") {
    boost::json::object reasoning;
    reasoning["effort"] = ass.reasoning_effort_;
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

  // The assistant's authentication token was collected by the form and persisted
  // to the settings file, but never reached the wire: every request went out
  // unauthenticated, which only worked while a proxy injected its own key or the
  // endpoint needed none. Omit the header when the field is empty so keyless
  // local endpoints are not handed a bogus `Bearer `.
  if (!ass.authentication_token_.empty()) {
    headers.push_back({"Authorization", "Bearer " + ass.authentication_token_});
  }

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
    const prompt_events& events,
    const std::function<std::vector<std::string>()>& take_steering) {

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
    //
    // one request produces 0 to many message rows.
    // A tool-only response with no reasoning item appends nothing from the model
    // at all — there is no row to hang usage on. And a response with three tool calls
    // appends three rows, so a usage column would either be written three times
    // (tripling it in any naive sum) or written on one arbitrarily chosen row,
    // which is a convention nothing enforces.
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
      caps,
      sess.response_budget().get());

    // Signal the start of this assistant message *before* any of its deltas. A
    // single turn runs one model call per tool-loop iteration, and each call
    // streams its own assistant message; without this boundary the UI would
    // concatenate one message's text directly onto the next (across the tool
    // calls between them), merging separate replies into a single paragraph
    // until the turn ended and the DB round-trip split them apart again.
    if (events.on_message_begin) events.on_message_begin();

    // Forward the visible text and hidden reasoning to the caller as they
    // stream. A prompt typed meanwhile is only *queued*; it is never delivered
    // mid-stream, so it cannot cut this reply short. The queue is drained after
    // the request's rows are stored, and the loop then runs one more iteration
    // so the model answers the queued prompts inside this same turn.
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

    const felis::net::llm::Result* v = result ? &*result : nullptr;

    if (v == nullptr) {
      ok = false;
      error = request_error;
      break;
    }

    // Measure this request against the turn's opening user message; reported
    // usage is exactly what the auto-compact predicate reads next round. This is
    // one row per request, not a column on a message: the rows appended below may
    // be none (tool-only reply, no reasoning item) or several (text + N calls), so
    // no single message owns this measurement. See the `request_usage` schema
    // comment in `database.cpp` for the full rationale.
    if (v != nullptr && v->usage.reported) {
      db_->record_request_usage(
        user_message_id,
        static_cast<std::uint32_t>(v->usage.input_tokens),
        static_cast<std::uint32_t>(v->usage.output_tokens));
    }

    // std::cout << std::format("Remote: {}\n", v.text);

    // Persist the reasoning item(s) this response produced so a later request can
    // replay them and the model can resume its chain-of-thought. Inserted *first*,
    // ahead of this turn's assistant text and tool calls, so the replayed
    // `reasoning` item lands before the output/`function_call` items it belongs
    // to. Which models deliver one is a property of the endpoint, not the family:
    // a Responses-protocol reasoning model may return an item with only an opaque
    // `encrypted_content` (GPT/o-series) or with a readable `summary`, and this
    // code stores whichever it received. A model that returns no `reasoning` item
    // (plain chat-completions output) simply produces no row.
    if (v != nullptr) {
      boost::json::array reasoning_items;
      std::set<std::string> seen_reasoning_ids;

      for (const boost::json::object& item : v->response_items) {
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

      // A chat-completions reasoning model (DeepSeek) streams its thinking through
      // `on_reasoning` instead of returning a `reasoning` item, so it would be
      // lost once the turn ended. Record it as a display-only item: with no
      // `encrypted_content` it is never replayed to the model.
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
        shadowed_message reasoning_row(
          std::string(), message_kind::assistant_reasoning_payload);
        reasoning_row.reasoning_payload_ = boost::json::serialize(envelope);
        append_message(session_id, reasoning_row);
      }
    }

    // The proxy rejects empty assistant text items, so only persist a reply
    // that actually carries something to replay.
    const std::string& reply_text = v->text;
    if (!reply_text.empty()) {
      append_message(session_id, shadowed_message(reply_text, message_kind::assistant_output));
    }

    if (v != nullptr && !v->tool_calls.empty()) {
      for (const auto& call : v->tool_calls) {
        append_message(
          session_id, shadowed_message(call.arguments, message_kind::assistant_tool_call, {call.id, call.name}));
      }

      for (const auto& call : v->tool_calls) {
        if (events.on_status) events.on_status("Running " + call.name);
        std::string tool_output = felis::bell::execute_local_tool(
          call,
          sess.shared_directories(),
          assistant_snapshot.proxy_,
          caps,
          stop);

        append_message(
          session_id, shadowed_message(tool_output, message_kind::tool_result, {call.id, call.name}));
      }
    }

    // Prompts typed while this request ran are taken now, after its rows (text,
    // calls, results). They travel from the GUI as plain strings, not as
    // already-stored rows, so the worker stores them here — id order is
    // conversation order.
    std::vector<std::string> steered_prompts;
    if (take_steering) {
      steered_prompts = take_steering();
    }

    if (!steered_prompts.empty()) {
      for (const std::string& prompt : steered_prompts) {
        const size_type steer_id =
          append_message(session_id, shadowed_message(prompt, message_kind::user_prompt));
        if (events.on_steered) events.on_steered(steer_id);
      }

      // Answer the queued prompts in this same turn instead of parking them
      // until the user sends again.
      idle = false;
    } else {
      idle = v->tool_calls.empty();
    }
  }

  if (events.on_done) {
    events.on_done(ok, error);
  }
}

bool felis::bell::client::update_memory(
    std::stop_token stop, const prompt_events& events) {
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
    if (events.on_done) events.on_done(false, "no new conversation to merge into memory yet");
    return false;
  }

  if (events.on_status) events.on_status("Updating memory\u2026");

  const boost::json::object payload = build_memory_request(
    assistant_snapshot.memory_, summaries, recent,
    assistant_snapshot.memory_budget_, assistant_snapshot);

  // The memory update is invisible context work: its deltas must never reach the
  // transcript. Swallow the text and keep the stream alive.
  felis::net::llm::StreamCallbacks callbacks;
  callbacks.on_text = [](std::string_view) { return true; };

  std::string request_error;
  const std::optional<felis::net::llm::Result> merged = stream_llm_request(
    stop, payload, assistant_snapshot, callbacks, request_error);

  if (!merged.has_value()) {
    felis::log::error("memory update request failed: {}", request_error);
    if (events.on_done) events.on_done(false, request_error);
    return false;
  }

  // Same guard as compaction: a reasoning model can spend the whole output budget
  // thinking and reply with a bare "\n\n". Overwriting the memory with that
  // would silently erase it.
  if (merged->text.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
    felis::log::error(
      "memory update produced a blank reply ({} output tokens); memory left unchanged",
      merged->usage.output_tokens);
    if (events.on_done) events.on_done(false, "the model returned no memory text");
    return false;
  }

  assistant updated = assistant_snapshot;
  updated.memory_ = merged->text;
  set_assistant(updated);

  if (events.on_done) events.on_done(true, std::string_view());
  return true;
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

  return error_code::SUCCESS;
}
