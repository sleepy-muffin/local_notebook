// Shape checks for the two request builders. Pure payload production: no
// database, no network, no display server.
//
// The important properties are that Chat Completions groups an assistant turn's
// text and tool calls into one message, answers each call with a `tool` message,
// and never emits a `tool` message for a call that is not in the window; and
// that the Responses shape is unchanged.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "message.hpp"
#include "request.hpp"
#include "store.hpp"

using namespace felis::bell;

static void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

static std::string text(const boost::json::value& value) {
  return std::string(value.as_string());
}

static std::vector<shadowed_message> sample_transcript() {
  return {
      shadowed_message("hello", message_kind::user_prompt),
      shadowed_message("let me check", message_kind::assistant_output),
      shadowed_message(
          R"({"path":"a.txt"})", message_kind::assistant_tool_call,
          tool_call_info{"call-1", "read_file"}),
      shadowed_message("contents", message_kind::tool_result,
                       tool_call_info{"call-1", "read_file"}),
      // An orphan result: its call is not in this window and must be dropped.
      shadowed_message("stale", message_kind::tool_result,
                       tool_call_info{"call-missing", "read_file"}),
      shadowed_message("done", message_kind::assistant_output),
  };
}

static assistant make_assistant(api_protocol protocol) {
  assistant ass;
  ass.model_ = "test-model";
  ass.protocol_ = protocol;
  return ass;
}

static void check_chat_shape() {
  const assistant ass = make_assistant(api_protocol::chat_completions);
  const boost::json::object payload = build_turn_request(
      ass, R"({"assistant_memory":""})", sample_transcript(), "high",
      tool_capabilities(0), 512);

  check(payload.if_contains("input") == nullptr, "chat payload has no `input`");
  check(payload.if_contains("store") == nullptr, "chat payload has no `store`");
  check(payload.at("max_tokens").to_number<std::int64_t>() == 512,
        "chat cap is `max_tokens`");
  check(text(payload.at("reasoning_effort")) == "high",
        "chat reasoning is top-level `reasoning_effort`");
  check(payload.if_contains("tools") == nullptr,
        "chat omits an empty `tools` array");

  // `[0]` system, `[1]` user, `[2]` assistant + call, `[3]` tool answer,
  // `[4]` follow-up.
  const boost::json::array& messages = payload.at("messages").as_array();
  check(messages.size() == 5, "chat transcript grouped into 5 messages");
  check(text(messages[0].as_object().at("role")) == "system",
        "instructions lead as system");
  check(text(messages[1].as_object().at("role")) == "user",
        "the prompt stays a user turn");

  const boost::json::object& call_turn = messages[2].as_object();
  check(text(call_turn.at("role")) == "assistant", "assistant turn role");
  check(text(call_turn.at("content")) == "let me check", "assistant text kept");
  check(call_turn.at("tool_calls").as_array().size() == 1,
        "assistant carries its tool call");
  check(text(call_turn.at("tool_calls").as_array()[0].as_object().at("id")) ==
            "call-1",
        "tool call keeps its id");
  check(text(call_turn.at("tool_calls")
                 .as_array()[0]
                 .as_object()
                 .at("function")
                 .as_object()
                 .at("name")) == "read_file",
        "tool call keeps its name");

  const boost::json::object& result_turn = messages[3].as_object();
  check(text(result_turn.at("role")) == "tool", "result answers as `tool`");
  check(text(result_turn.at("tool_call_id")) == "call-1",
        "result quotes the call id");
  check(text(result_turn.at("content")) == "contents", "result content kept");

  check(text(messages[4].as_object().at("content")) == "done",
        "follow-up text is its own turn");
}

static void check_responses_shape() {
  const assistant ass = make_assistant(api_protocol::responses);
  const boost::json::object payload = build_turn_request(
      ass, "instructions", sample_transcript(), "none",
      tool_capabilities(0), 512);

  check(payload.if_contains("messages") == nullptr,
        "responses payload has no `messages`");
  check(payload.if_contains("input") != nullptr, "responses uses `input`");
  check(payload.at("store").as_bool() == false, "responses keeps `store:false`");
  check(payload.at("max_output_tokens").to_number<std::int64_t>() == 512,
        "responses cap is `max_output_tokens`");
  check(payload.if_contains("reasoning") == nullptr,
        "responses omits reasoning when effort is none");
  check(text(payload.at("instructions")) == "instructions",
        "responses instructions stay top-level");

  const boost::json::array& input = payload.at("input").as_array();
  check(input.size() == 6, "responses keeps every row as an input item");
  check(text(input[2].as_object().at("type")) == "function_call",
        "responses call item");
  check(text(input[3].as_object().at("type")) == "function_call_output",
        "responses result item");
}

static shadowed_message stored_reasoning() {
  shadowed_message row(std::string(), message_kind::assistant_reasoning_payload);
  row.reasoning_payload_ = R"({"reasoning_items":[{"type":"reasoning","id":"rs_1","encrypted_content":"opaque-blob"}]})";
  return row;
}

static void check_compaction_shape() {
  // The summariser only needs readable turns: a stored reasoning row must be
  // filtered out of both shapes (it is pointless input and some gateways reject
  // it when reasoning is off).
  std::vector<shadowed_message> msgs = sample_transcript();
  msgs.push_back(stored_reasoning());

  const assistant chat = make_assistant(api_protocol::chat_completions);
  const boost::json::object chat_payload = build_compaction_request(
      "summarise rules", msgs, 300, chat, "none");

  check(chat_payload.if_contains("input") == nullptr,
        "chat compaction has no `input`");
  check(chat_payload.if_contains("tools") == nullptr,
        "compaction carries no tools");
  check(chat_payload.at("max_tokens").to_number<std::int64_t>() == 300,
        "chat compaction cap is `max_tokens`");
  check(chat_payload.if_contains("reasoning_effort") == nullptr,
        "chat compaction omits an explicit `none` effort");
  check(chat_payload.if_contains("stream_options") != nullptr,
        "chat compaction asks for usage");

  const boost::json::array& messages = chat_payload.at("messages").as_array();
  check(text(messages[0].as_object().at("role")) == "system" &&
            text(messages[0].as_object().at("content")) == "summarise rules",
        "chat compaction leads with the summariser system turn");
  const boost::json::object& closing = messages.back().as_object();
  check(text(closing.at("role")) == "user",
        "chat compaction ends on a user turn");
  check(text(closing.at("content")).find("Summarize the entire conversation") !=
            std::string::npos,
        "chat compaction ends with the closing instruction");
  check(boost::json::serialize(chat_payload).find("opaque-blob") ==
            std::string::npos,
        "chat compaction drops stored reasoning");

  const assistant responses = make_assistant(api_protocol::responses);
  const boost::json::object responses_payload = build_compaction_request(
      "summarise rules", msgs, 300, responses, "none");

  check(responses_payload.if_contains("messages") == nullptr,
        "responses compaction has no `messages`");
  check(responses_payload.if_contains("tools") == nullptr,
        "responses compaction carries no tools");
  check(text(responses_payload.at("instructions")) == "summarise rules",
        "responses compaction keeps instructions top-level");
  check(responses_payload.at("max_output_tokens").to_number<std::int64_t>() == 300,
        "responses compaction cap is `max_output_tokens`");

  const boost::json::array& input = responses_payload.at("input").as_array();
  check(text(input.back().as_object().at("role")) == "user",
        "responses compaction ends on a user turn");
  check(boost::json::serialize(responses_payload).find("opaque-blob") ==
            std::string::npos,
        "responses compaction drops stored reasoning");
}

static void check_memory_shape() {
  const std::vector<session_summary> summaries{
      session_summary(size_type(1), "A session", "what happened")};
  const std::vector<shadowed_message> recent{
      shadowed_message("newest question", message_kind::user_prompt),
      shadowed_message("newest answer", message_kind::assistant_output)};

  assistant chat = make_assistant(api_protocol::chat_completions);
  chat.reasoning_effort_ = "low";
  const boost::json::object chat_payload = build_memory_request(
      "old memory", summaries, recent, 400, chat);

  check(chat_payload.if_contains("input") == nullptr,
        "chat memory has no `input`");
  check(chat_payload.if_contains("tools") == nullptr,
        "memory carries no tools");
  check(chat_payload.at("max_tokens").to_number<std::int64_t>() == 400,
        "chat memory cap is `max_tokens`");
  check(text(chat_payload.at("reasoning_effort")) == "low",
        "chat memory uses top-level `reasoning_effort`");

  const boost::json::array& messages = chat_payload.at("messages").as_array();
  check(text(messages[0].as_object().at("role")) == "system",
        "chat memory leads with a system turn");
  check(text(messages[0].as_object().at("content")).find("long-term memory") !=
            std::string::npos,
        "chat memory system turn states the merge rules");
  check(text(messages[1].as_object().at("role")) == "user" &&
            text(messages[1].as_object().at("content")).find("old memory") !=
                std::string::npos,
        "chat memory carries the existing memory as context");
  check(text(messages.back().as_object().at("role")) == "user" &&
            text(messages.back().as_object().at("content")).find(
                "Write the updated memory now") != std::string::npos,
        "chat memory ends with the closing instruction");

  assistant responses = make_assistant(api_protocol::responses);
  responses.reasoning_effort_ = "low";
  const boost::json::object responses_payload = build_memory_request(
      "old memory", summaries, recent, 400, responses);

  check(responses_payload.if_contains("messages") == nullptr,
        "responses memory has no `messages`");
  check(responses_payload.if_contains("tools") == nullptr,
        "responses memory carries no tools");
  check(responses_payload.at("max_output_tokens").to_number<std::int64_t>() == 400,
        "responses memory cap is `max_output_tokens`");
  check(text(responses_payload.at("instructions")).find("long-term memory") !=
            std::string::npos,
        "responses memory keeps the merge rules as instructions");
  check(text(responses_payload.at("reasoning").as_object().at("effort")) == "low",
        "responses memory uses `reasoning:{effort}`");
}

int main() {
  check_chat_shape();
  check_responses_shape();
  check_compaction_shape();
  check_memory_shape();
  std::puts("request shape checks passed");
  return 0;
}
