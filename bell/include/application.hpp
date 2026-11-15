#pragma once

#include <string>
#include <optional>
#include <cstdint>

#include "common.hpp"

namespace felis::bell{

struct shadowed_message{

  shadowed_message(uint32_t id, const std::string& content, message_kind kind) : id_(id), content_(content), kind_(kind) {}
  shadowed_message(const std::string& content, message_kind kind) : content_(content), kind_(kind) {}
  shadowed_message(
    const std::string& content,
    message_kind kind,
    const tool_call_info& tool_call
  ) : content_(content), kind_(kind), tool_call_(tool_call) {}

  uint32_t id_;
  std::string content_;
  std::optional<std::string> reasoning_payload_;

  uint32_t session_id_;

  message_kind kind_;

  // ISO-8601 UTC; sortable, so sessions can be
  // interleaved by time for assistant memory. Empty for a message that has not
  // been stored yet.
  std::string made_at_;

  // The id of the inline summary that absorbed this message, or 0 while it is
  // still part of the live context. The transcript shows it beside the message
  // id so a reader can see which summary folded a row away.
  uint32_t compacted_at_ = 0;

  std::optional<tool_call_info> tool_call_;

};

struct shadowed_session{
  uint32_t id_;
  std::string title_;
  // Pinned sessions get their own sidebar band above the rest.
  bool pinned_ = false;

  shadowed_session(uint32_t id, const std::string title, bool pinned = false)
    : id_(id), title_(title), pinned_(pinned) {}
};

}
