#include "request.hpp"

#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <felis/log.hpp>

namespace felis::bell {

// Shared closing turns. Both protocols need them, and both must ask in exactly
// the same words: an explicit user turn at the end is what stops a transcript
// tipped by a tool result or an assistant reply from being continued instead of
// summarised.
constexpr std::string_view SUMMARIZER_CLOSING_TURN =
    "Summarize the entire conversation above as a running summary for "
    "continuation. Output only the summary text \u2014 no preamble, no tool "
    "calls, no questions.";

constexpr std::string_view MEMORY_INSTRUCTIONS =
    "You maintain the assistant's long-term memory: a short note carried into "
    "every future session. You are given the memory as it stands, each session's "
    "own summary, and the newest messages across sessions. Rewrite the memory so "
    "it absorbs the new material without growing without bound; omit what is no "
    "longer relevant. Never invent facts that are not in the material above.";

constexpr std::string_view MEMORY_CLOSING_TURN =
    "Write the updated memory now: one compact Markdown note that keeps every "
    "standing fact, decision, preference, open task and file path, drops small "
    "talk and anything already stale, and merges duplicates. Output only the "
    "memory text \u2014 no preamble, no tool calls, no questions.";

// A chat request only asks for reasoning when a level is actually requested.
// "none" is deliberately omitted rather than sent: OpenAI rejects an explicit
// "none" effort, and a model that does not reason ignores the field anyway.
[[nodiscard]] bool chat_reasoning_requested(const std::string& reasoning_effort) {
  return !reasoning_effort.empty() && reasoning_effort != "none";
}

// The output cap. Chat Completions names it `max_tokens`; that spelling is the
// one local servers (llama.cpp, vLLM, Ollama) and most proxies accept. A caller
// below the cap limit omits the field and lets the provider use its default.
void set_chat_output_cap(boost::json::object& payload, std::size_t max_output_tokens) {
  if (max_output_tokens > 0) {
    payload["max_tokens"] = static_cast<std::int64_t>(max_output_tokens);
  }
}

// Chat streams need to be asked for a usage block explicitly; without it the
// terminal chunk carries no counts and per-request accounting silently drops.
void request_stream_usage(boost::json::object& payload) {
  boost::json::object stream_options;
  stream_options["include_usage"] = true;
  payload["stream_options"] = std::move(stream_options);
}

// A stored reasoning payload is only replayable under the Responses protocol,
// and only when it carries the provider's opaque `encrypted_content`. That blob
// is the only part a strict gateway can decode back into the chain of thought,
// so an item without it is not worth the input tokens: Responses has nothing to
// match it against. Chat Completions has no equivalent field at all, so its
// builder never looks at these rows.
bool payload_has_replayable_reasoning(const std::string& payload) {
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

boost::json::array build_responses_input(
  const std::vector<shadowed_message>& msgs
) {
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

// Chat Completions flattens what Responses keeps as sibling items: an assistant
// text row and the calls it requested are one assistant message, and each call's
// result is a `role:"tool"` message quoting its `tool_call_id`. The run is
// therefore emitted lazily — it stays open until a tool result, user turn, or
// summary closes it — so the rows that the transcript stores separately land on
// the message the provider expects.
boost::json::array build_chat_messages(
  const std::vector<shadowed_message>& msgs
) {
  boost::json::array messages;

  boost::json::object assistant;
  boost::json::array tool_calls;
  std::string assistant_text;
  bool has_assistant = false;
  bool has_content = false;
  bool has_calls = false;

  std::set<std::string> issued_calls;
  std::set<std::string> answered_calls;

  const auto flush_assistant = [&]() {
    if (!has_assistant) {
      return;
    }

    assistant["content"] = has_content ? assistant_text : std::string();
    if (has_calls) {
      assistant["tool_calls"] = std::move(tool_calls);
    }

    messages.emplace_back(std::move(assistant));

    assistant = boost::json::object{};
    tool_calls = boost::json::array{};
    assistant_text.clear();
    has_assistant = false;
    has_content = false;
    has_calls = false;
  };

  for (const shadowed_message& msg: msgs) {
    switch (msg.kind_) {
      case message_kind::user_prompt: {
        flush_assistant();

        boost::json::object entry;
        entry["role"] = "user";
        entry["content"] = msg.content_;
        messages.emplace_back(std::move(entry));
        break;
      }

      case message_kind::assistant_output: {
        // Text that follows a tool-call run belongs to the next assistant turn.
        if (has_calls) {
          flush_assistant();
        }
        if (!has_assistant) {
          assistant["role"] = "assistant";
          has_assistant = true;
        }
        if (has_content) {
          assistant_text += "\n\n";
        }
        assistant_text += msg.content_;
        has_content = true;
        break;
      }

      case message_kind::assistant_tool_call: {
        if (!has_assistant) {
          assistant["role"] = "assistant";
          has_assistant = true;
        }

        const std::string& call_id = msg.tool_call_.value().tool_call_id_;

        boost::json::object function;
        function["name"] = msg.tool_call_.value().tool_name_;
        function["arguments"] = msg.content_;   // raw JSON string from ToolCall::arguments

        boost::json::object call;
        call["id"] = call_id;
        call["type"] = "function";
        call["function"] = std::move(function);

        tool_calls.emplace_back(std::move(call));
        issued_calls.insert(call_id);
        has_calls = true;
        break;
      }

      case message_kind::tool_result: {
        flush_assistant();

        const std::string& call_id = msg.tool_call_.value().tool_call_id_;

        // A result whose call fell outside the request window (or was already
        // answered) cannot be quoted back: providers reject a `tool` message with
        // no matching call, so skip it rather than fail the whole turn.
        if (!issued_calls.contains(call_id) || !answered_calls.insert(call_id).second) {
          felis::log::info(
            "dropping tool result {} with no matching call in this request", call_id);
          break;
        }

        boost::json::object entry;
        entry["role"] = "tool";
        entry["tool_call_id"] = call_id;
        entry["content"] = msg.content_;
        messages.emplace_back(std::move(entry));
        break;
      }

      case message_kind::inline_compact: {
        // The folded summary leads the conversation as prior context, exactly as
        // it does in the Responses shape.
        flush_assistant();

        boost::json::object entry;
        entry["role"] = "assistant";
        entry["content"] = msg.content_;
        messages.emplace_back(std::move(entry));
        break;
      }

      case message_kind::assistant_reasoning_payload: {
        // Chat Completions has no replayable reasoning field: the visible
        // thinking came through `reasoning_content` deltas and was kept for
        // display only, so it contributes nothing to the next request.
        break;
      }

      default: {
        break;
      }
    }
  }

  flush_assistant();

  return messages;
}

std::string build_custom_instructions(
  const std::string& session_custom_instructions,
  const std::string& assistant_custom_instructions,
  const std::string& assistant_memory
)
{
  boost::json::object entry;
  entry["session_custom_instructions"] = session_custom_instructions;
  entry["assistant_custom_instructions"] = assistant_custom_instructions;
  entry["assistant_memory"] = assistant_memory;
  std::string result = boost::json::serialize(entry);

  return result;
}

boost::json::object build_chat_request(
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& model,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
) {
  boost::json::array messages;

  // Chat has no top-level instruction field; the instructions become the system
  // message that leads the conversation.
  if (!custom_instructions.empty()) {
    boost::json::object entry;
    entry["role"] = "system";
    entry["content"] = custom_instructions;
    messages.emplace_back(std::move(entry));
  }

  for (boost::json::value& entry: build_chat_messages(msgs)) {
    messages.emplace_back(std::move(entry));
  }

  boost::json::object payload;
  payload["model"] = model;
  payload["messages"] = std::move(messages);
  payload["stream"] = true;
  request_stream_usage(payload);

  // The session's response budget caps how many tokens the model may produce for
  // this request. Without it the provider falls back to its own default, which
  // silently ignores the number the session editor shows. 0 disables the cap.
  set_chat_output_cap(payload, max_output_tokens);

  // Some servers reject an empty `tools` array, so omit the field entirely when
  // the session grants no tools. Chat uses the nested `function` schema directly.
  const boost::json::array tools = tool_definitions(capabilities);
  if (!tools.empty()) {
    payload["tools"] = tools;
  }

  // Reasoning is a top-level option here (`reasoning_effort`), unlike Responses'
  // `reasoning:{effort}` object.
  if (chat_reasoning_requested(reasoning_effort)) {
    payload["reasoning_effort"] = reasoning_effort;
  }

  return payload;
}

boost::json::object build_responses_request(
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& model,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
) {
  boost::json::object payload;
  payload["model"] = model;
  payload["input"] = build_responses_input(msgs);
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
  for (const shadowed_message& msg : msgs) {
    if (msg.kind_ == message_kind::assistant_reasoning_payload
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

boost::json::object build_turn_request(
  const assistant& ass,
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
) {
  if (ass.protocol_ == api_protocol::chat_completions) {
    return build_chat_request(
      custom_instructions, msgs, ass.model_, reasoning_effort, capabilities,
      max_output_tokens);
  }

  if (ass.protocol_ == api_protocol::responses) {
    return build_responses_request(
      custom_instructions, msgs, ass.model_, reasoning_effort, capabilities,
      max_output_tokens);
  }

  felis::raise("Undefined Protocol");
  return {};
}

boost::json::object build_compaction_request(
  const std::string& summarizer_instructions,
  const std::vector<shadowed_message>& msgs,
  std::size_t max_output_tokens,
  const assistant& ass,
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
  // budget is spent) instead of summarising. The instructions alone do not
  // override that continuation reflex. A trailing user instruction does:
  // verified against the proxy, it turns a blank/task-continuation reply into a
  // clean "## Conversation Summary".
  // The summariser only needs the readable transcript, so drop the stored
  // reasoning-item rows: replaying `encrypted_content` into a summarisation turn
  // (usually run at `effort:"none"`) is pointless and some gateways reject a
  // reasoning input item when reasoning is disabled. Chat's builder skips these
  // rows anyway; filtering here keeps both shapes identical.
  std::vector<shadowed_message> readable;
  readable.reserve(msgs.size());
  for (const shadowed_message& msg : msgs) {
    if (msg.kind_ != message_kind::assistant_reasoning_payload) {
      readable.push_back(msg);
    }
  }

  if (ass.protocol_ == api_protocol::chat_completions) {
    boost::json::array messages;

    boost::json::object system;
    system["role"] = "system";
    system["content"] = summarizer_instructions;
    messages.emplace_back(std::move(system));

    for (boost::json::value& entry: build_chat_messages(readable)) {
      messages.emplace_back(std::move(entry));
    }

    boost::json::object closing_turn;
    closing_turn["role"] = "user";
    closing_turn["content"] = SUMMARIZER_CLOSING_TURN;
    messages.emplace_back(std::move(closing_turn));

    payload["messages"] = std::move(messages);
    payload["stream"] = true;
    request_stream_usage(payload);
    set_chat_output_cap(payload, max_output_tokens);

    // The summariser must not spend its output budget on hidden reasoning: a
    // reasoning model that thinks through the whole budget replies with blank (or
    // truncated) text, and `compact()`'s blank-summary guard then throws the whole
    // compaction away. Honour the session's dedicated summarisation reasoning
    // level; "none"/empty is omitted because OpenAI rejects an explicit "none".
    if (chat_reasoning_requested(auto_compact_reasoning_effort)) {
      payload["reasoning_effort"] = auto_compact_reasoning_effort;
    }

    return payload;
  }


  if (ass.protocol_ == api_protocol::responses) {

    boost::json::array summary_input = build_responses_input(readable);
    {
      boost::json::object closing_turn;
      closing_turn["role"] = "user";
      closing_turn["content"] = SUMMARIZER_CLOSING_TURN;
      summary_input.emplace_back(std::move(closing_turn));
    }
    payload["input"] = std::move(summary_input);
    payload["stream"] = true;
    payload["instructions"] = summarizer_instructions;

    if (max_output_tokens > 0) {
      payload["max_output_tokens"] = static_cast<std::int64_t>(max_output_tokens);
    }

    // This is a SESSION setting, not the assistant's: chat turns are unaffected,
    // and the inline summary always runs on the same model as the chat.
    if (!auto_compact_reasoning_effort.empty()) {
      boost::json::object reasoning;
      reasoning["effort"] = auto_compact_reasoning_effort;
      payload["reasoning"] = std::move(reasoning);
    }

    return payload;

  }

  felis::raise("Undefined Protocol");
  return {};
}

boost::json::object build_memory_request(
  const std::string& current_memory,
  const std::vector<session_summary>& summaries,
  const std::vector<shadowed_message>& recent_messages,
  std::size_t max_output_tokens,
  const assistant& ass
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
  for (const session_summary& summary : summaries) {
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
  for (const shadowed_message& msg : recent_messages) {
    boost::json::object entry;
    entry["role"] = msg.kind_ == message_kind::user_prompt
        ? "user"
        : "assistant";
    entry["content"] = msg.content_;
    input.emplace_back(std::move(entry));
  }

  {
    boost::json::object closing_turn;
    closing_turn["role"] = "user";
    closing_turn["content"] = MEMORY_CLOSING_TURN;
    input.emplace_back(std::move(closing_turn));
  }

  boost::json::object payload;
  payload["model"] = ass.model_;

  if (ass.protocol_ == api_protocol::chat_completions) {
    // Chat wants the merge rules as a leading system message and everything else
    // as ordinary turns; `input` is already exactly that list.
    boost::json::array messages;

    boost::json::object system;
    system["role"] = "system";
    system["content"] = MEMORY_INSTRUCTIONS;
    messages.emplace_back(std::move(system));

    for (boost::json::value& entry: input) {
      messages.emplace_back(std::move(entry));
    }

    payload["messages"] = std::move(messages);
    payload["stream"] = true;
    request_stream_usage(payload);
    set_chat_output_cap(payload, max_output_tokens);

    // Mirrors the Responses path below: the assistant's own level caps the
    // merge, with empty/"none" omitted because OpenAI rejects an explicit
    // "none" effort.
    if (chat_reasoning_requested(ass.reasoning_effort_)) {
      payload["reasoning_effort"] = ass.reasoning_effort_;
    }

    return payload;
  }
  if (ass.protocol_ == api_protocol::responses) {

    payload["input"] = std::move(input);
    payload["stream"] = true;
    payload["instructions"] = MEMORY_INSTRUCTIONS;

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

  felis::raise("Undefined Protocol");
  return {};
}

} // namespace felis::bell
