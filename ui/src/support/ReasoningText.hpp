#pragma once

#include <string>

// Turns a stored `assistant_reasoning_payload` row into the text the transcript
// shows. The row is the wire-shaped `{"reasoning_items":[...]}` envelope the
// model returned, so the readable parts live in each item's `summary[].text` and
// `content[].text`.
namespace ui::reasoning {

// Concatenates every readable text part, in order, separated by blank lines.
// No truncation: DeepSeek's chat-completions thinking arrives this way and the
// user asked to see it whole. An item that carries only encrypted content (a
// GPT/o-series reasoning item) has no readable part, so when nothing readable is
// found the payload is returned verbatim as JSON — that is the "show the plain
// json" case. Empty or malformed input yields an empty string.
std::string display_text(const std::string& payload);

} // namespace ui::reasoning
