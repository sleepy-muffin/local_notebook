#include <cstdio>
#include <string>

#include "common.hpp"
#include "support/LlmPayloadFormat.hpp"

namespace payload_test {

std::string dump(const std::string& body) {
  std::string text(felis::bell::IDENTIFIER_LLM_PAYLOAD);
  text.push_back('\n');
  text += body;
  return text;
}

bool is_plain_log() {
  if (ui::llm_payload::is_dump("client: ordinary record")) {
    std::fprintf(stderr, "FAIL: a plain record was taken for a payload\n");
    return false;
  }

  return true;
}

// A Responses request carrying a hidden reasoning item, a function call and an
// `instructions` field that is itself JSON.
bool renders_responses_request() {
  const std::string body = R"({"model":"gpt-6-luna","input":[{"role":"user","content":"Hello?"},{"type":"reasoning","id":"rs_1","summary":[],"content":[],"encrypted_content":"gAAAA-secret-blob"},{"type":"function_call","id":"fc_1","call_id":"call_1","name":"read_file","arguments":"{\"path\":\"a.txt\"}"},{"type":"function_call_output","call_id":"call_1","output":"contents"}],"stream":true,"max_output_tokens":8000,"tools":[{"type":"function","name":"read_file","description":"Reads a file","parameters":{"type":"object","properties":{"path":{"type":"string"}}}}],"instructions":"{\"session_custom_instructions\":\"be kind\",\"assistant_memory\":\"\"}"})";

  const std::string content = dump(body);

  if (!ui::llm_payload::is_dump(content)) {
    std::fprintf(stderr, "FAIL: payload dump was not recognised\n");
    return false;
  }

  const QString rendered = ui::llm_payload::render(content);
  const std::string text = rendered.toStdString();

  const char* expected[] = {
      "gpt-6-luna", "Messages", "Hello?", "reasoning", "encrypted_content: 17 bytes",
      "read_file", "tool output", "contents", "Parameters", "max_output_tokens",
      "be kind", "Tools", "Reads a file", "Raw",
  };

  for (const char* needle : expected) {
    if (text.find(needle) == std::string::npos) {
      std::fprintf(stderr, "FAIL: rendered payload lacks %s\n", needle);
      return false;
    }
  }

  // The opaque blob must never be printed, only described.
  if (text.find("gAAAA-secret-blob") != std::string::npos) {
    std::fprintf(stderr, "FAIL: encrypted reasoning was printed in full\n");
    return false;
  }

  return true;
}

// A chat-completions reasoning model stores its thinking as typed
// `reasoning_text` parts; the wording (not the JSON) must be shown.
bool renders_readable_reasoning() {
  const std::string body = R"({"model":"deepseek-flash","input":[{"type":"reasoning","content":[{"type":"reasoning_text","text":"pondering the question"}]}]})";
  const std::string text = ui::llm_payload::render(dump(body)).toStdString();

  if (text.find("pondering the question") == std::string::npos) {
    std::fprintf(stderr, "FAIL: readable reasoning text was not rendered\n");
    return false;
  }

  // The raw tail keeps the exact wire body as well.
  if (text.find("Raw") == std::string::npos) {
    std::fprintf(stderr, "FAIL: raw tail is missing\n");
    return false;
  }

  return true;
}

bool titles_the_request() {
  const std::string content =
      dump(R"({"model":"gpt-6-luna","input":[{},{},{}],"tools":[]})");
  const QString title = ui::llm_payload::title(content);

  if (!title.contains(QStringLiteral("gpt-6-luna")) ||
      !title.contains(QStringLiteral("3"))) {
    std::fprintf(stderr, "FAIL: title did not summarise the payload\n");
    return false;
  }

  return true;
}

bool survives_malformed_json() {
  const std::string content = dump("{not json");
  const std::string rendered = ui::llm_payload::render(content).toStdString();

  if (rendered != "{not json") {
    std::fprintf(stderr, "FAIL: malformed payload was not passed through\n");
    return false;
  }

  return true;
}

} // namespace payload_test

int main() {
  using namespace payload_test;

  if (!is_plain_log() || !renders_responses_request() ||
      !renders_readable_reasoning() || !titles_the_request() ||
      !survives_malformed_json()) {
    return 1;
  }

  std::puts("llm-payload-format: ok");
  return 0;
}
