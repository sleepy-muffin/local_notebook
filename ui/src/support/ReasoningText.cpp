#include "support/ReasoningText.hpp"

#include <boost/json.hpp>

#include <string>

namespace ui::reasoning {

namespace {

void append_part(std::string& out, const std::string& text) {
  if (text.empty()) {
    return;
  }

  if (!out.empty()) {
    out += "\n\n";
  }

  out += text;
}

// Appends `item[key][].text` for a `summary` or `content` array.
void append_parts(std::string& out, const boost::json::object& item, const char* key) {
  const boost::json::value* parts = item.if_contains(key);
  if (parts == nullptr || !parts->is_array()) {
    return;
  }

  for (const boost::json::value& part: parts->as_array()) {
    if (!part.is_object()) {
      continue;
    }

    const boost::json::value* text = part.as_object().if_contains("text");
    if (text != nullptr && text->is_string()) {
      append_part(out, std::string(text->as_string()));
    }
  }
}

} // namespace

std::string display_text(const std::string& payload) {
  if (payload.empty()) {
    return {};
  }

  boost::system::error_code ec;
  const boost::json::value parsed = boost::json::parse(payload, ec);
  if (ec || !parsed.is_object()) {
    return {};
  }

  const boost::json::value* items = parsed.as_object().if_contains("reasoning_items");
  if (items == nullptr || !items->is_array()) {
    return {};
  }

  std::string out;
  bool any_item = false;

  for (const boost::json::value& item: items->as_array()) {
    if (!item.is_object()) {
      continue;
    }

    any_item = true;
    append_parts(out, item.as_object(), "summary");
    append_parts(out, item.as_object(), "content");
  }

  if (!out.empty()) {
    // A provider can send a summary whose only text is whitespace (see the
    // single-space `summary_text` some chat-completions replies return). That
    // draws a title with no body, so treat it as no readable content.
    if (out.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
      return {};
    }

    return out;
  }

  // Encrypted-only items carry no readable text. Show the payload as-is rather
  // than an empty strip, so the row still says something happened.
  return any_item ? boost::json::serialize(parsed) : std::string{};
}

} // namespace ui::reasoning
