#pragma once

// Rendering for the request bodies the backend dumps behind
// `felis::bell::IDENTIFIER_LLM_PAYLOAD`. The log inspector shows these records
// as readable cards (model, parameters, conversation, tool definitions) instead
// of a wall of JSON, mirroring the standalone `ai_request_inspector.py` layout.
//
// The renderer is Qt-side but otherwise pure: it never touches widgets, so the
// logger callback may call it from its worker thread.

#include <QString>

#include <string>

namespace ui::llm_payload {

// True when `content` is a payload dump: the identifier line followed by the
// serialised body.
bool is_dump(const std::string& content);

// A one-line description for the inspector's list ("gpt-6-luna · 6 input ·
// 0 tools"). Falls back to the first body line when the shape is unknown.
QString title(const std::string& content);

// A readable rendition of the dumped body. Anything that cannot be parsed is
// returned verbatim, so a malformed dump is still inspectable.
QString render(const std::string& content);

} // namespace ui::llm_payload
