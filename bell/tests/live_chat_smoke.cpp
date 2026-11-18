// Live smoke test for both wire protocols against a running OpenAI-compatible
// endpoint. Not registered with ctest: it needs a proxy on the network.
//
// It drives the same `build_turn_request` / `felis::net::llm::stream_request`
// path the application uses, so what goes on the wire is exactly what a real
// turn sends. A two-round exchange checks the parts that only appear when a turn
// is continued: Chat must group the assistant text with its `tool_calls` and
// answer them with `role:"tool"` messages, while Responses must replay the
// reasoning item it received.
//
//   live-chat-smoke [base_url]
//
// `base_url` defaults to http://127.0.0.1:37203/v1 (the local gateway).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include <felis/net/llm.hpp>

#include "application.hpp"
#include "message.hpp"
#include "request.hpp"
#include "store.hpp"
#include "tools.hpp"

using namespace felis::bell;

static constexpr const char* kModels[] = {"deepseek-flash", "gpt-6-luna", "gpt-6.1-sol"};

struct streamed {
  felis::net::llm::Result result;
  bool ok = false;
  std::string error;
};

static streamed stream_once(const assistant& ass, const boost::json::object& payload) {
  streamed out;

  const std::string body = boost::json::serialize(payload);
  std::vector<felis::net::HttpHeader> headers{
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
  };

  const bool chat = ass.protocol_ == api_protocol::chat_completions;

  auto result = felis::net::llm::stream_request(
      chat ? felis::net::llm::Protocol::ChatCompletions
           : felis::net::llm::Protocol::Responses,
      ass.api_url_ + (chat ? "/chat/completions" : "/responses"),
      std::nullopt, headers, body, std::stop_token{}, 60'000, 320'000, {});

  if (!result) {
    out.error = result.error().reason;
    return out;
  }

  out.result = std::move(result.value());
  out.ok = true;
  return out;
}

static std::string preview(const std::string& text, std::size_t limit = 120) {
  std::string one_line;
  for (char c: text) {
    one_line.push_back(c == '\n' ? ' ' : c);
  }
  if (one_line.size() > limit) {
    one_line.resize(limit);
    one_line += "...";
  }
  return one_line;
}

// Mirrors `client::prompt`'s bookkeeping: a reasoning row first, then the reply
// text, then one row per tool call, then one row per tool result. Returns true
// when the turn produced a reasoning item with opaque `encrypted_content`, which
// the continued request must therefore replay.
static bool append_turn(std::vector<shadowed_message>& transcript,
                        const felis::net::llm::Result& result) {
  boost::json::array reasoning_items;
  bool has_encrypted = false;

  for (const boost::json::object& item: result.response_items) {
    const boost::json::value* type = item.if_contains("type");
    const std::string kind =
        type != nullptr && type->is_string() ? std::string(type->as_string()) : "?";
    const boost::json::value* encrypted = item.if_contains("encrypted_content");
    const std::size_t encrypted_size =
        encrypted != nullptr && encrypted->is_string() ? encrypted->as_string().size() : 0;
    std::printf("  output item: type=%s encrypted=%zu summary=%s\n", kind.c_str(),
                encrypted_size,
                item.if_contains("summary") != nullptr ? "yes" : "no");
    if (type != nullptr && type->is_string() && type->as_string() == "reasoning") {
      if (encrypted_size > 0) {
        has_encrypted = true;
      }
      reasoning_items.emplace_back(item);
    }
  }
  if (reasoning_items.empty() && !result.reasoning.empty()) {
    boost::json::object content;
    content["type"] = "reasoning_text";
    content["text"] = result.reasoning;
    boost::json::object synthetic;
    synthetic["type"] = "reasoning";
    synthetic["content"] = boost::json::array{std::move(content)};
    reasoning_items.emplace_back(std::move(synthetic));
  }
  if (!reasoning_items.empty()) {
    boost::json::object envelope;
    envelope["reasoning_items"] = std::move(reasoning_items);
    shadowed_message row(std::string(), message_kind::assistant_reasoning_payload);
    row.reasoning_payload_ = boost::json::serialize(envelope);
    std::printf("  stored reasoning row (%zu bytes)\n",
                row.reasoning_payload_->size());
    transcript.push_back(std::move(row));
  }

  if (!result.text.empty()) {
    transcript.push_back(
        shadowed_message(result.text, message_kind::assistant_output));
  }

  for (const felis::net::llm::ToolCall& call: result.tool_calls) {
    transcript.push_back(shadowed_message(
        call.arguments, message_kind::assistant_tool_call,
        tool_call_info{call.id, call.name}));

    // The probe does not touch the filesystem: it answers the call itself, the
    // same way a successful local tool would.
    transcript.push_back(shadowed_message(
        "a.txt contains: hello from the mountain", message_kind::tool_result,
        tool_call_info{call.id, call.name}));
  }

  return has_encrypted;
}

static bool run_one(const std::string& base_url, const std::string& model,
                    api_protocol protocol) {
  // What the next request actually carries, so a silent drop is visible. Returns
  // false only when the protocol's replay contract is broken.
  const auto report_replay = [](const boost::json::object& payload, api_protocol p,
                                bool expect_encrypted) {
    if (p == api_protocol::responses) {
      std::size_t reasoning = 0;
      std::size_t encrypted_bytes = 0;
      if (const boost::json::value* input = payload.if_contains("input");
          input != nullptr && input->is_array()) {
        for (const boost::json::value& item: input->as_array()) {
          if (!item.is_object() || item.as_object().if_contains("type") == nullptr
              || item.as_object().at("type") != "reasoning") {
            continue;
          }
          ++reasoning;
          if (const boost::json::value* encrypted =
                  item.as_object().if_contains("encrypted_content");
              encrypted != nullptr && encrypted->is_string()) {
            encrypted_bytes += encrypted->as_string().size();
          }
        }
      }
      const bool include = payload.if_contains("include") != nullptr;
      std::printf("  replay: input reasoning items=%zu encrypted=%zu include=%s\n",
                  reasoning, encrypted_bytes, include ? "yes" : "no");

      // The summary-only reasoning item must be dropped (nothing to replay), but
      // an encrypted one must survive, and `include` must always ride along.
      if (expect_encrypted && (reasoning == 0 || encrypted_bytes == 0)) {
        std::printf("  replay FAILED: encrypted reasoning was not replayed\n");
        return false;
      }
      if (reasoning > 0 && !include) {
        std::printf("  replay FAILED: reasoning replayed without include\n");
        return false;
      }
    } else {
      std::size_t calls = 0;
      std::size_t reasoning_fields = 0;
      if (const boost::json::value* messages = payload.if_contains("messages");
          messages != nullptr && messages->is_array()) {
        for (const boost::json::value& message: messages->as_array()) {
          if (!message.is_object()) {
            continue;
          }
          const boost::json::object& object = message.as_object();
          const boost::json::value* role = object.if_contains("role");
          if (role != nullptr && role->is_string() && role->as_string() == "assistant"
              && object.if_contains("tool_calls") != nullptr) {
            ++calls;
          }
          if (object.if_contains("reasoning_content") != nullptr
              || object.if_contains("reasoning") != nullptr) {
            ++reasoning_fields;
          }
        }
      }
      std::printf("  replay: assistant messages with tool_calls=%zu reasoning fields=%zu\n",
                  calls, reasoning_fields);

      // Chat Completions has no replayable reasoning field; sending one would be
      // rejected by stricter providers. The reasoning row must be dropped.
      if (reasoning_fields > 0) {
        std::printf("  replay FAILED: chat request carried a reasoning field\n");
        return false;
      }
      if (expect_encrypted) {
        std::printf("  replay FAILED: chat turn stored encrypted reasoning\n");
        return false;
      }
    }

    return true;
  };
  const char* name =
      protocol == api_protocol::chat_completions ? "chat" : "responses";

  assistant ass;
  ass.name_ = "probe";
  ass.api_url_ = base_url;
  ass.model_ = model;
  ass.protocol_ = protocol;

  const tool_capabilities caps(
      static_cast<tool_capabilities::store_type>(tool_capability::repo_read));

  std::vector<shadowed_message> transcript;
  transcript.push_back(shadowed_message(
      "Use the read_file tool on a.txt, then quote what it returned.",
      message_kind::user_prompt));

  std::printf("\n=== %s / %s ===\n", model.c_str(), name);

  // Round 1: expect a tool call.
  const boost::json::object first = build_turn_request(
      ass, R"({"assistant_memory":""})", transcript, "medium", caps, 1024);
  const streamed r1 = stream_once(ass, first);
  if (!r1.ok) {
    std::printf("  round 1 FAILED: %s\n", r1.error.c_str());
    return false;
  }
  std::printf("  round 1: text=%zu reasoning=%zu tool_calls=%zu usage=%zu/%zu\n",
              r1.result.text.size(), r1.result.reasoning.size(),
              r1.result.tool_calls.size(), r1.result.usage.input_tokens,
              r1.result.usage.output_tokens);

  const bool expect_encrypted = append_turn(transcript, r1.result);

  if (r1.result.tool_calls.empty()) {
    // Still continue the conversation: the protocol must accept the transcript
    // even without a tool round, and this becomes a plain second exchange.
    transcript.push_back(
        shadowed_message("And what was in the file?", message_kind::user_prompt));
  }

  // Round 2: the continued turn, carrying the assistant turn (and its call /
  // result rows, or the replayed reasoning item) back to the provider.
  const boost::json::object second = build_turn_request(
      ass, R"({"assistant_memory":""})", transcript, "medium", caps, 1024);
  const bool replay_ok = report_replay(second, protocol, expect_encrypted);
  const streamed r2 = stream_once(ass, second);
  if (!r2.ok) {
    std::printf("  round 2 FAILED: %s\n", r2.error.c_str());
    return false;
  }
  std::printf("  round 2: text=%zu reasoning=%zu tool_calls=%zu usage=%zu/%zu\n",
              r2.result.text.size(), r2.result.reasoning.size(),
              r2.result.tool_calls.size(), r2.result.usage.input_tokens,
              r2.result.usage.output_tokens);

  if (r2.result.text.empty() && r2.result.tool_calls.empty()) {
    std::printf("  round 2 FAILED: empty reply\n");
    return false;
  }
  std::printf("  reply: %s\n", preview(r2.result.text).c_str());
  return replay_ok;
}

// The two invisible-context requests must accept either wire shape too. They
// run on the same assistant, so a protocol switch has to cover them as well:
// Chat takes the rules as a leading system message, Responses as `instructions`.
static bool run_auxiliary(const std::string& base_url, const std::string& model,
                          api_protocol protocol) {
  const char* name =
      protocol == api_protocol::chat_completions ? "chat" : "responses";

  assistant ass;
  ass.name_ = "probe";
  ass.api_url_ = base_url;
  ass.model_ = model;
  ass.protocol_ = protocol;

  std::printf("\n=== %s / %s / compaction + memory ===\n", model.c_str(), name);

  const std::vector<shadowed_message> history{
      shadowed_message("What is in a.txt?", message_kind::user_prompt),
      shadowed_message("It holds a greeting.", message_kind::assistant_output)};

  const boost::json::object compact = build_compaction_request(
      "You summarise conversations.", history, 512, ass, "none");
  const streamed c = stream_once(ass, compact);
  if (!c.ok) {
    std::printf("  compaction FAILED: %s\n", c.error.c_str());
    return false;
  }
  std::printf("  compaction: text=%zu reasoning=%zu\n", c.result.text.size(),
              c.result.reasoning.size());

  const std::vector<session_summary> summaries{
      session_summary{size_type(1), "Earlier", "The user asked about a file."}};
  const std::vector<shadowed_message> recent{
      shadowed_message("Anything else?", message_kind::user_prompt),
      shadowed_message("Only that.", message_kind::assistant_output)};

  const boost::json::object memory = build_memory_request(
      "The user likes coffee.", summaries, recent, 512, ass);
  const streamed m = stream_once(ass, memory);
  if (!m.ok) {
    std::printf("  memory FAILED: %s\n", m.error.c_str());
    return false;
  }
  std::printf("  memory: text=%zu reasoning=%zu\n", m.result.text.size(),
              m.result.reasoning.size());

  const bool ok = !c.result.text.empty() && !m.result.text.empty();
  if (!ok) {
    std::printf("  auxiliary FAILED: a reply was empty\n");
  }
  return ok;
}

int main(int argc, char** argv) {
  const std::string base_url =
      argc > 1 ? argv[1] : "http://127.0.0.1:37203/v1";

  int failures = 0;
  for (const char* model: kModels) {
    for (const api_protocol protocol:
         {api_protocol::chat_completions, api_protocol::responses}) {
      if (!run_one(base_url, model, protocol)) {
        ++failures;
      }
      if (!run_auxiliary(base_url, model, protocol)) {
        ++failures;
      }
    }
  }

  std::printf("\n%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
