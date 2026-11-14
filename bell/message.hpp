#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include <felis/log.hpp>

#include "application.hpp"
#include "common.hpp"

namespace felis::bell {

class database;
struct database_access;



enum class tool_capability : std::uint32_t {
    online = 1 << 0,
    repo_read   = 1 << 1,
    repo_write  = 1 << 2,
    bash   = 1 << 3,
};

class tool_capabilities
{
public:
  using store_type = std::uint32_t;

  tool_capabilities(store_type store) : capabilities_(store) {}


  store_type raw() const { return capabilities_; }
  bool empty() const { return capabilities_ == 0; }

  bool online() const { return has_capability(tool_capability::online); }
  bool repo_read() const { return has_capability(tool_capability::repo_read); }
  bool repo_write() const { return has_capability(tool_capability::repo_write); }

private:
  // constexpr tool_capabilities operator|(
  //     tool_capability lhs,
  //     tool_capability rhs
  // ) {
  //     return static_cast<tool_capabilities>(lhs)
  //          | static_cast<tool_capabilities>(rhs);
  // }

  constexpr bool has_capability(tool_capability capability) const {
      return (capabilities_ & static_cast<store_type>(capability)) != 0;
  }

  store_type capabilities_;

};



// Use its object as const when possible.
// This struct should be constructed when demanded.
//
// `reasoning_payload_`
//
// Opaque reasoning handle for a Reasoning part. `text_` holds the visible
// reasoning text (what a DeepSeek-style model streams as `reasoning_content`);
// this holds the non-human-readable token a GPT-style model returns instead —
// the encrypted reasoning item / `reasoning_details`, replayed verbatim so the
// model keeps its own train of thought across turns. Empty for models that
// reason in the clear, and for every non-reasoning message.
//
// class message {
// public:
//
//   size_type id() const {
//     return id_;
//   }
//   size_type session_id() const {
//     return session_id_;
//   }
//
//   // Updates the message text and, when this message was loaded from a database,
//   // writes the change straight through to its row (see `database::update_message`).
//   void set_content(const std::string& text);
//
//   std::string content() const {
//     felis::check(content_ != nullptr, "`text_` is nullptr");
//     return *content_;
//   }
//
//   std::optional<std::string> reasoning_payload() const {
//     if (reasoning_payload_ == nullptr) {
//       return std::nullopt;
//     }
//
//     return *reasoning_payload_;
//   }
//
//   message(
//     const database& db,
//     size_type id,
//     size_type session_id,
//     message_kind message_kind,
//     const std::string& text,
//     std::string made_at,
//     std::uint32_t summarized_at,
//     std::string tool_call_id,
//     std::string tool_name,
//     std::optional<std::string> reasoning_payload
//   ) :
//     db_(db),
//     kind_(message_kind),
//     id_(std::move(id)),
//     session_id_(std::move(session_id)),
//     summarized_at_(summarized_at == 0 ? size_type{} : size_type(summarized_at)),
//     content_(std::make_unique<std::string>(text)),
//     reasoning_payload_(
//       reasoning_payload.has_value()
//       ? std::make_unique<std::string>(reasoning_payload.value())
//       : nullptr
//     ),
//     made_at_(std::move(made_at)),
//     tool_call_id_(std::move(tool_call_id)),
//     tool_name_(std::move(tool_name))
//   {}
//
//
//   auto kind() const { return kind_; }
//   auto tool_call_id() const { return tool_call_id_; }
//   auto tool_name() const { return tool_name_; }
//
// private:
//   const database& db_;
//
//   message_kind kind_;
//
//   // `0` is not used in sqlite database, so when it's `0`, it means unset.
//   size_type id_;
//   size_type session_id_;
//
//   // When this message has been used for summarization in this session,
//   // it must not be reused unless displaying or only last summarization verbatim window.
//   // It's usually 0.
//   size_type summarized_at_;
//
//   std::unique_ptr<std::string> content_;
//   std::unique_ptr<std::string> reasoning_payload_;
//
//   // Revoke this one to time span format
//   std::string made_at_;
//
//   // The typed-part view of a message (see message_kind). Tool calls and their
//   // results carry the call they answer and, for a call, the tool's name.
//   std::string tool_call_id_;
//   std::string tool_name_;
// };


// This struct should be constructed when demanded.
class session
{
public:

  session() = delete;

  session(
    size_type&& id,
    const std::string& title,
    const std::string& custom_instructions,
    const std::string& reasoning_effort,
    const std::string& auto_compact_reasoning_effort,
    std::vector<std::string> shared_directories,
    session_kind kind,
    bool pinned,
    tool_capabilities tools,
    size_type auto_compact_threshold,
    size_type auto_compact_preserve,
    size_type auto_compact_budget,
    size_type response_budget
  ) :
    id_(std::move(id)),
    title_(title),
    custom_instructions_(custom_instructions),
    reasoning_effort_(reasoning_effort),
    auto_compact_reasoning_effort_(auto_compact_reasoning_effort),
    shared_directories_(std::move(shared_directories)),
    kind_(kind),
    pinned_(pinned),
    tools_(tools),
    auto_compact_threshold_(auto_compact_threshold),
    auto_compact_preserve_(auto_compact_preserve),
    auto_compact_budget_(auto_compact_budget),
    response_budget_(response_budget)
  {
  }

  size_type id() const {
    return id_;
  }


  auto title() const {return title_;}
  auto custom_instructions() const {return custom_instructions_;}
  auto reasoning_effort() const {return reasoning_effort_;}
  auto auto_compact_reasoning_effort() const {return auto_compact_reasoning_effort_;}
  auto shared_directories() const {return shared_directories_;}
  auto kind() const {return kind_;}
  auto pinned() const {return pinned_;}

  auto tools() const {return tools_;}

  auto auto_compact_threshold() const {return auto_compact_threshold_;}
  auto auto_compact_preserve() const {return auto_compact_preserve_;}
  auto auto_compact_budget() const {return auto_compact_budget_;}
  auto response_budget() const {return response_budget_;}

private:
  size_type id_;

  std::string title_;
  std::string custom_instructions_;

  std::string reasoning_effort_;
  // The reasoning level used when this session's inline summary is generated. It
  // is independent of `reasoning_effort_` (the chat turns) and of the assistant,
  // so a reasoning model can summarise at a low/none effort and still emit text.
  std::string auto_compact_reasoning_effort_;
  std::vector<std::string> shared_directories_;


  session_kind kind_;

  // Pinned sessions are listed in their own sidebar category above the rest.
  bool pinned_;

  // Tool permissions for this session.
  tool_capabilities tools_;

  // The input bound and the collapse trigger.
  size_type auto_compact_threshold_;
  size_type auto_compact_preserve_;
  size_type auto_compact_budget_;

  // The whole turn's budget for produced tokens.
  size_type response_budget_;
};

} // namespace felis::bell
