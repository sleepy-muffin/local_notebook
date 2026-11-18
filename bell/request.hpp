#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "application.hpp"
#include "database.hpp"
#include "store.hpp"
#include "tools.hpp"

namespace felis::bell {

// Request-body construction for the two OpenAI wire protocols.
//
// The database stores a protocol-neutral transcript (`shadowed_message`), so
// these builders are the only place that knows how a protocol spells a turn:
// Responses replays `input` items (`function_call` / `function_call_output` /
// reasoning items), Chat Completions fills one `messages` array (assistant
// `tool_calls` plus `role:"tool"` answers). Everything above this layer asks for
// a payload and lets `assistant::protocol_` decide which shape it gets.

// The session's instructions, the assistant's instructions and its memory,
// serialised as a single JSON document. Serialising (rather than concatenating)
// prevents user text from escaping into the instruction region.
std::string build_custom_instructions(
  const std::string& session_custom_instructions,
  const std::string& assistant_custom_instructions,
  const std::string& assistant_memory
);

boost::json::object build_chat_request(
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& model,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
);

boost::json::object build_responses_request(
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& model,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
);

// The one request a chat turn makes; the assistant's protocol selects the
// shape. Callers do not branch on the protocol.
boost::json::object build_turn_request(
  const assistant& ass,
  const std::string& custom_instructions,
  const std::vector<shadowed_message>& msgs,
  const std::string& reasoning_effort,
  tool_capabilities capabilities,
  std::size_t max_output_tokens
);

// Builds the single request that folds the eligible history into a summary. It
// carries no tools (the model must only summarize, never act) and a dedicated
// instruction, and is capped by the session's compaction output budget. The
// assistant's protocol selects the wire shape.
boost::json::object build_compaction_request(
  const std::string& summarizer_instructions,
  const std::vector<shadowed_message>& msgs,
  std::size_t max_output_tokens,
  const assistant& ass,
  const std::string& auto_compact_reasoning_effort
);

// Builds the single request that merges the assistant's memory. It carries no
// tools (the model must only merge, never act), states the merge rules as the
// instructions, and ends with an explicit user turn asking for the result. The
// assistant's protocol selects the wire shape.
boost::json::object build_memory_request(
  const std::string& current_memory,
  const std::vector<session_summary>& summaries,
  const std::vector<shadowed_message>& recent_messages,
  std::size_t max_output_tokens,
  const assistant& ass
);

} // namespace felis::bell
