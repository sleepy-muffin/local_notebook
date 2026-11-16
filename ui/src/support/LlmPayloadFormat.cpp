#include "support/LlmPayloadFormat.hpp"

#include <boost/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

#include <QStringList>

#include "common.hpp"
#include "support/i18n.hpp"

namespace ui::llm_payload {

namespace llm_payload_detail {

namespace json = boost::json;

// Long strings (opaque blobs, pasted documents) are cut here so one field cannot
// push the conversation out of reach. `encrypted_content` is handled separately
// and never printed at all.
constexpr std::size_t kStringLimit = 2000;
constexpr int kIndentWidth = 2;
constexpr std::string_view kRule =
    "────────────────────────────────────────────────────────────────";

std::string_view identifier() {
  return felis::bell::IDENTIFIER_LLM_PAYLOAD;
}

QString translate(const char* text) {
  return trs(text);
}

// The payload body without the identifier line.
std::string_view body_of(std::string_view content) {
  const std::string_view id = identifier();

  if (content.starts_with(id) && content.size() > id.size() &&
      content[id.size()] == '\n') {
    return content.substr(id.size() + 1);
  }

  return content;
}

const json::value* member(const json::object& object, std::string_view key) {
  return object.if_contains(key);
}

const json::string* string_member(const json::object& object, std::string_view key) {
  const json::value* value = member(object, key);

  if (value != nullptr && value->is_string()) {
    return &value->as_string();
  }

  return nullptr;
}

std::string to_std(const json::string& text) {
  return std::string(text.data(), text.size());
}

std::string compact(const json::value& value) {
  return json::serialize(value);
}

// Serialise `value` across multiple lines so nested request fields stay legible.
void write_pretty(
    std::string& out, const json::value& value, int depth
);

void write_string(std::string& out, const json::string& text) {
  const std::size_t size = text.size();

  if (size > kStringLimit) {
    out.push_back('"');
    out.append(text.data(), kStringLimit);
    out += "…\" (";
    out += std::to_string(size);
    out += " chars)";
    return;
  }

  out += compact(json::value(text));
}

void write_padded(std::string& out, int depth) {
  out.append(static_cast<std::size_t>(depth) * kIndentWidth, ' ');
}

void write_pretty(std::string& out, const json::value& value, int depth) {
  if (value.is_object()) {
    const json::object& object = value.as_object();

    if (object.empty()) {
      out += "{}";
      return;
    }

    out += "{\n";
    bool first = true;

    for (const json::object::value_type& entry : object) {
      if (!first) {
        out += ",\n";
      }
      first = false;
      write_padded(out, depth + 1);
      write_string(out, json::string(entry.key()));
      out += ": ";

      // The opaque blob is never printed, even in the raw tail.
      if (entry.key() == "encrypted_content" && entry.value().is_string()) {
        out += "<";
        out += std::to_string(entry.value().as_string().size());
        out += " bytes hidden>";
      } else {
        write_pretty(out, entry.value(), depth + 1);
      }
    }

    out.push_back('\n');
    write_padded(out, depth);
    out.push_back('}');
    return;
  }

  if (value.is_array()) {
    const json::array& array = value.as_array();

    if (array.empty()) {
      out += "[]";
      return;
    }

    out += "[\n";
    bool first = true;

    for (const json::value& item : array) {
      if (!first) {
        out += ",\n";
      }
      first = false;
      write_padded(out, depth + 1);
      write_pretty(out, item, depth + 1);
    }

    out.push_back('\n');
    write_padded(out, depth);
    out.push_back(']');
    return;
  }

  if (value.is_string()) {
    write_string(out, value.as_string());
    return;
  }

  out += compact(value);
}

// Append multi-line text, prefixing every line so it sits under its heading.
void append_indented(std::string& out, std::string_view text, int spaces) {
  const std::string pad(static_cast<std::size_t>(spaces), ' ');
  std::size_t start = 0;

  while (start <= text.size()) {
    const std::size_t newline = text.find('\n', start);
    const std::size_t end = newline == std::string_view::npos ? text.size() : newline;

    out += pad;
    out.append(text.data() + start, end - start);
    out.push_back('\n');

    if (newline == std::string_view::npos) {
      break;
    }

    start = newline + 1;
  }
}

// Flatten a message `content` field: a string, null, or a list of typed parts.
std::string flatten_content(const json::value& content) {
  if (content.is_null()) {
    return {};
  }

  if (content.is_string()) {
    return to_std(content.as_string());
  }

  if (!content.is_array()) {
    return compact(content);
  }

  std::string out;
  bool first = true;

  for (const json::value& part : content.as_array()) {
    if (!first) {
      out.push_back('\n');
    }
    first = false;

    if (part.is_string()) {
      out += to_std(part.as_string());
      continue;
    }

    if (!part.is_object()) {
      out += compact(part);
      continue;
    }

    const json::object& object = part.as_object();
    const json::string* type = string_member(object, "type");

    if (type != nullptr &&
        (to_std(*type) == "text" || to_std(*type) == "input_text" ||
         to_std(*type) == "output_text" || to_std(*type) == "reasoning_text" ||
         to_std(*type) == "summary_text")) {
      if (const json::string* text = string_member(object, "text")) {
        out += to_std(*text);
        continue;
      }
    }

    out += '[';
    out += type != nullptr ? to_std(*type) : "?";
    out += "] ";
    write_pretty(out, part, 0);
  }

  return out;
}

// `encrypted_content` is opaque, and enormous. Show that it exists and how big
// it is, never the base64 itself. The readable parts (`summary`, `content`, or a
// plain `reasoning_content`/`text` field for chat-completions models such as
// DeepSeek) are flattened so their wording shows, not a wall of JSON.
void append_reasoning_item(
    std::string& out, const json::object& object, int spaces
) {
  if (const json::string* id = string_member(object, "id")) {
    append_indented(out, "id: " + to_std(*id), spaces);
  }

  bool wrote = false;

  for (std::string_view key : {"summary", "content"}) {
    const json::value* value = member(object, key);

    if (value == nullptr) {
      continue;
    }

    const std::string text = flatten_content(*value);

    if (text.empty()) {
      continue;
    }

    append_indented(out, text, spaces);
    wrote = true;
  }

  // A reasoning model that streams its thinking directly stores it as a plain
  // string rather than as typed summary/content parts.
  for (std::string_view key : {"reasoning_content", "reasoning", "text"}) {
    const json::string* value = string_member(object, key);

    if (value == nullptr || value->empty()) {
      continue;
    }

    append_indented(out, to_std(*value), spaces);
    wrote = true;
  }

  if (const json::string* encrypted = string_member(object, "encrypted_content")) {
    append_indented(
        out, "encrypted_content: " + std::to_string(encrypted->size()) +
                 " bytes (" + translate("hidden").toStdString() + ")",
        spaces
    );
    wrote = true;
  }

  if (!wrote) {
    append_indented(out, translate("(empty)").toStdString(), spaces);
  }
}

void append_tool_calls(std::string& out, const json::value& calls, int spaces);

// One chat-style tool call: `function.name` plus its arguments, pretty-printed
// when they are JSON.
void append_tool_call(std::string& out, const json::object& call, int spaces) {
  const json::value* function_value = member(call, "function");
  const json::object* function =
      function_value != nullptr && function_value->is_object()
          ? &function_value->as_object()
          : nullptr;

  std::string name = translate("tool call").toStdString();
  std::string arguments;

  if (function != nullptr) {
    if (const json::string* text = string_member(*function, "name")) {
      name = to_std(*text);
    }
    if (const json::string* text = string_member(*function, "arguments")) {
      arguments = to_std(*text);
    }
  }

  append_indented(out, "⚙ " + name, spaces);

  if (const json::string* id = string_member(call, "id")) {
    append_indented(out, "id: " + to_std(*id), spaces + 2);
  }

  if (!arguments.empty()) {
    std::string rendered = arguments;

    try {
      std::string pretty;
      write_pretty(pretty, json::parse(arguments), 0);
      rendered = pretty;
    } catch (...) {
      // Not JSON: show the raw arguments.
    }

    append_indented(out, rendered, spaces + 2);
  }
}

void append_tool_calls(std::string& out, const json::value& calls, int spaces) {
  if (!calls.is_array()) {
    return;
  }

  for (const json::value& call : calls.as_array()) {
    if (call.is_object()) {
      append_tool_call(out, call.as_object(), spaces);
    }
  }
}

void append_message(
    std::string& out, const json::object& object, std::size_t index,
    std::string_view role
) {
  out += '[';
  out += std::to_string(index);
  out += "] ";
  out += role;

  if (const json::string* name = string_member(object, "name")) {
    out += "  name=";
    out += to_std(*name);
  }

  if (const json::string* call_id = string_member(object, "tool_call_id")) {
    out += "  tool_call_id=";
    out += to_std(*call_id);
  }

  if (const json::string* effort = string_member(object, "reasoning_effort")) {
    out += "  reasoning_effort=";
    out += to_std(*effort);
  }

  out.push_back('\n');

  const json::value* reasoning = member(object, "reasoning");

  if (reasoning == nullptr) {
    reasoning = member(object, "reasoning_content");
  }

  if (reasoning != nullptr && reasoning->is_string() &&
      !reasoning->as_string().empty()) {
    append_indented(out, "(reasoning)", 4);
    append_indented(out, to_std(reasoning->as_string()), 4);
  }

  if (const json::value* content = member(object, "content")) {
    const std::string text = flatten_content(*content);

    if (!text.empty()) {
      append_indented(out, text, 4);
    }
  }

  if (const json::value* calls = member(object, "tool_calls")) {
    append_tool_calls(out, *calls, 4);
  }

  out.push_back('\n');
}

// A Responses-protocol item with a `type` rather than a `role`.
void append_typed_item(
    std::string& out, const json::object& object, std::size_t index,
    std::string_view type
) {
  out += '[';
  out += std::to_string(index);
  out += "] ";

  if (type == "reasoning") {
    out += translate("reasoning").toStdString();
    out.push_back('\n');
    append_reasoning_item(out, object, 4);
    out.push_back('\n');
    return;
  }

  if (type == "function_call") {
    out += translate("tool call").toStdString();

    if (const json::string* name = string_member(object, "name")) {
      out += " → ";
      out += to_std(*name);
    }

    out.push_back('\n');

    if (const json::string* id = string_member(object, "id")) {
      append_indented(out, "id: " + to_std(*id), 4);
    }
    if (const json::string* call_id = string_member(object, "call_id")) {
      append_indented(out, "call_id: " + to_std(*call_id), 4);
    }

    if (const json::string* arguments = string_member(object, "arguments")) {
      std::string pretty;

      try {
        write_pretty(pretty, json::parse(to_std(*arguments)), 0);
      } catch (...) {
        pretty = to_std(*arguments);
      }

      append_indented(out, pretty, 4);
    }

    out.push_back('\n');
    return;
  }

  if (type == "function_call_output") {
    out += translate("tool output").toStdString();
    out.push_back('\n');

    if (const json::string* call_id = string_member(object, "call_id")) {
      append_indented(out, "call_id: " + to_std(*call_id), 4);
    }

    if (const json::value* output = member(object, "output")) {
      append_indented(out, flatten_content(*output), 4);
    }

    out.push_back('\n');
    return;
  }

  out += type;
  out.push_back('\n');
  std::string pretty;
  write_pretty(pretty, json::value(object), 0);
  append_indented(out, pretty, 4);
  out.push_back('\n');
}

void append_item(std::string& out, const json::value& item, std::size_t index) {
  if (!item.is_object()) {
    out += '[';
    out += std::to_string(index);
    out += "] ?\n";
    append_indented(out, compact(item), 4);
    out.push_back('\n');
    return;
  }

  const json::object& object = item.as_object();

  if (const json::string* role = string_member(object, "role")) {
    append_message(out, object, index, to_std(*role));
    return;
  }

  if (const json::string* type = string_member(object, "type")) {
    append_typed_item(out, object, index, to_std(*type));
    return;
  }

  out += '[';
  out += std::to_string(index);
  out += "] ?\n";
  std::string pretty;
  write_pretty(pretty, item, 0);
  append_indented(out, pretty, 4);
  out.push_back('\n');
}

// The array of turns: `input` for the Responses API, `messages` for chat.
const json::array* conversation(const json::object& body) {
  for (std::string_view key : {"input", "messages"}) {
    const json::value* value = member(body, key);

    if (value != nullptr && value->is_array()) {
      return &value->as_array();
    }
  }

  return nullptr;
}

void append_parameters(std::string& out, const json::object& body) {
  bool wrote = false;

  for (const json::object::value_type& entry : body) {
    const std::string_view key = entry.key();

    if (key == "input" || key == "messages" || key == "tools") {
      continue;
    }

    if (!wrote) {
      out += translate("Parameters").toStdString();
      out += '\n';
      out += kRule;
      out += '\n';
      wrote = true;
    }

    out += key;
    out += ":\n";

    // `instructions` is itself a JSON document; decode it so the three parts
    // (session, assistant, memory) read as fields instead of one escaped blob.
    if (key == "instructions" && entry.value().is_string()) {
      const std::string text = to_std(entry.value().as_string());

      try {
        std::string pretty;
        write_pretty(pretty, json::parse(text), 0);
        append_indented(out, pretty, kIndentWidth);
        out.push_back('\n');
        continue;
      } catch (...) {
        // Not JSON: fall through to the ordinary rendering.
      }
    }

    std::string pretty;
    write_pretty(pretty, entry.value(), 0);
    append_indented(out, pretty, kIndentWidth);
    out.push_back('\n');
  }

  if (wrote) {
    out.push_back('\n');
  }
}

void append_tools(std::string& out, const json::value& tools) {
  out += translate("Tools").toStdString();
  out += '\n';
  out += kRule;
  out += '\n';

  if (!tools.is_array() || tools.as_array().empty()) {
    out += translate("(none)").toStdString();
    out += '\n';
    return;
  }

  for (const json::value& tool : tools.as_array()) {
    if (!tool.is_object()) {
      continue;
    }

    const json::object& object = tool.as_object();
    const json::value* function_value = member(object, "function");
    const json::object* function =
        function_value != nullptr && function_value->is_object()
            ? &function_value->as_object()
            : &object;

    std::string name = "tool";

    if (const json::string* text = string_member(*function, "name")) {
      name = to_std(*text);
    }

    out += "⚙ ";
    out += name;
    out.push_back('\n');

    if (const json::string* description = string_member(*function, "description")) {
      append_indented(out, to_std(*description), kIndentWidth);
    }

    const json::value* parameters = member(*function, "parameters");

    if (parameters != nullptr && parameters->is_object()) {
      const json::value* properties = member(parameters->as_object(), "properties");

      if (properties != nullptr && properties->is_object()) {
        for (const json::object::value_type& property : properties->as_object()) {
          out += "  ";
          out += property.key();

          if (property.value().is_object()) {
            if (const json::string* type =
                    string_member(property.value().as_object(), "type")) {
              out += " (";
              out += to_std(*type);
              out += ")";
            }
          }

          out.push_back('\n');
        }
      }
    }
  }
}

QString finish(const std::string& text) {
  return QString::fromStdString(text);
}

} // namespace llm_payload_detail

using namespace llm_payload_detail;

bool is_dump(const std::string& content) {
  const std::string_view text = content;
  const std::string_view id = identifier();

  return text.starts_with(id) && text.size() > id.size() &&
         text[id.size()] == '\n';
}

QString title(const std::string& content) {
  const std::string body(body_of(content));

  try {
    const json::value root = json::parse(body);

    if (!root.is_object()) {
      return QString::fromStdString(compact(root));
    }

    const json::object& object = root.as_object();
    QStringList parts;

    if (const json::string* model = string_member(object, "model")) {
      parts << QString::fromStdString(to_std(*model));
    }

    if (const json::array* turns = conversation(object)) {
      parts << trs("%1 input").arg(static_cast<int>(turns->size()));
    }

    if (const json::value* tools = member(object, "tools");
        tools != nullptr && tools->is_array()) {
      parts << trs("%1 tools").arg(static_cast<int>(tools->as_array().size()));
    }

    if (!parts.isEmpty()) {
      return parts.join(QStringLiteral("  ·  "));
    }
  } catch (...) {
    // Fall through to a plain preview.
  }

  return QString::fromStdString(body).section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString render(const std::string& content) {
  const std::string_view raw = body_of(content);
  const std::string body(raw);

  try {
    const json::value root = json::parse(body);

    if (!root.is_object()) {
      return finish(body);
    }

    const json::object& object = root.as_object();
    std::string out;

    out += translate("LLM request").toStdString();
    out += '\n';
    out += kRule;
    out += '\n';

    const json::string* model = string_member(object, "model");

    out += translate("model").toStdString();
    out += ": ";
    out += model != nullptr ? to_std(*model) : "-";

    if (const json::value* stream = member(object, "stream");
        stream != nullptr && stream->is_bool()) {
      out += "    stream: ";
      out += stream->as_bool() ? "true" : "false";
    }

    if (const json::array* turns = conversation(object)) {
      out += "    ";
      out += std::to_string(turns->size());
      out += ' ';
      out += translate("items").toStdString();
    }

    out += "\n\n";

    if (const json::array* turns = conversation(object)) {
      out += translate("Messages").toStdString();
      out += '\n';
      out += kRule;
      out += '\n';

      if (turns->empty()) {
        out += translate("(empty)").toStdString();
        out.push_back('\n');
      } else {
        std::size_t index = 1;

        for (const json::value& item : *turns) {
          append_item(out, item, index);
          ++index;
        }
      }

      out.push_back('\n');
    }

    append_parameters(out, object);

    if (const json::value* tools = member(object, "tools")) {
      append_tools(out, *tools);
    }

    // The exact wire body, so anything the readable sections elide can still be
    // inspected. Long strings (opaque blobs) are shortened by `write_pretty`.
    out += translate("Raw").toStdString();
    out += '\n';
    out += kRule;
    out += '\n';
    write_pretty(out, root, 0);
    out.push_back('\n');

    return finish(out);
  } catch (...) {
    return finish(body);
  }
}

} // namespace ui::llm_payload
