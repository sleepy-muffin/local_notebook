#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>

#include <felis/log.hpp>

namespace felis::bell {
#ifndef LOCAL_NOTEBOOK_DEBUG
#define LOCAL_NOTEBOOK_DEBUG (false)
#endif

// Marks a dumped LLM request body in the log stream: the identifier line, then
// the serialised payload on the following lines.
constexpr std::string_view IDENTIFIER_LLM_PAYLOAD = "local_notebook:llm_payload";

enum class error_code : std::uint32_t {
  SUCCESS = 0,


  ERR_INVALID_SESSION_ID = 100,

  // Why a turn or a memory update stopped short. The set is meant to be a retry
  // policy, not a mirror of every code path: cancellation is the user's choice,
  // a transport or stream death is worth another attempt, an HTTP status is
  // usually not (the endpoint answered), and the deterministic failures below
  // never are.
  ERR_CANCELLED = 101,
  ERR_REQUEST_TRANSPORT = 102,
  ERR_REQUEST_HTTP = 103,
  ERR_REQUEST_STREAM = 104,
  ERR_COMPACTION_FAILED = 105,
  ERR_BLANK_REPLY = 106,
  ERR_NOTHING_TO_UPDATE = 107,
  // The turn left through a path that does not return a result (an escaping
  // exception). Worth another attempt only when the attempt stored nothing, so
  // the caller also checks that no new row appeared before re-running.
  ERR_INTERNAL = 108,
  ERR_SESSION_SUMMARY_TRIGGER_HAS_MET = 200,
  ERR_ASSISTANT_MEMORY_TRIGGER_HAS_MET = 201,

  // Another task already owns the session (one turn or fold at a time), or the
  // shared assistant fold is running and would read this session's rows.
  ERR_SESSION_BUSY = 202,
  ERR_ASSISTANT_BUSY = 203,
  ERR_ASSISTANT_OR_ANY_SESSION_BUSY = 205,
  // The instance lock could not be taken: usually another process has the
  // assistant open, but it can also be a filesystem error. The accompanying
  // message always says which.
  ERR_ASSISTANT_LOCK_FAILED = 206,


};

// A stable name for each code, for logs and for the retry decisions below.
[[nodiscard]] inline const char* to_string(error_code code) noexcept {
  switch (code) {
    case error_code::SUCCESS:
      return "SUCCESS";
    case error_code::ERR_INVALID_SESSION_ID:
      return "ERR_INVALID_SESSION_ID";
    case error_code::ERR_CANCELLED:
      return "ERR_CANCELLED";
    case error_code::ERR_REQUEST_TRANSPORT:
      return "ERR_REQUEST_TRANSPORT";
    case error_code::ERR_REQUEST_HTTP:
      return "ERR_REQUEST_HTTP";
    case error_code::ERR_REQUEST_STREAM:
      return "ERR_REQUEST_STREAM";
    case error_code::ERR_COMPACTION_FAILED:
      return "ERR_COMPACTION_FAILED";
    case error_code::ERR_BLANK_REPLY:
      return "ERR_BLANK_REPLY";
    case error_code::ERR_NOTHING_TO_UPDATE:
      return "ERR_NOTHING_TO_UPDATE";
    case error_code::ERR_INTERNAL:
      return "ERR_INTERNAL";
    case error_code::ERR_SESSION_SUMMARY_TRIGGER_HAS_MET:
      return "ERR_SESSION_SUMMARY_TRIGGER_HAS_MET";
    case error_code::ERR_ASSISTANT_MEMORY_TRIGGER_HAS_MET:
      return "ERR_ASSISTANT_MEMORY_TRIGGER_HAS_MET";
    case error_code::ERR_SESSION_BUSY:
      return "ERR_SESSION_BUSY";
    case error_code::ERR_ASSISTANT_BUSY:
      return "ERR_ASSISTANT_BUSY";
    case error_code::ERR_ASSISTANT_OR_ANY_SESSION_BUSY:
      return "ERR_ASSISTANT_OR_ANY_SESSION_BUSY";
    case error_code::ERR_ASSISTANT_LOCK_FAILED:
      return "ERR_ASSISTANT_LOCK_FAILED";
  }
  return "ERR_UNKNOWN";
}

// How many times `client::prompt` should re-attempt a turn that stopped short,
// and how long to wait between attempts. `max_retries` counts re-attempts after
// the first, so 0 disables retrying. Only failures another attempt can plausibly
// cure are retried: a dead transport or stream, and an unknown exit that stored
// no new row. A cancellation is the user's choice, an HTTP status is the
// endpoint's answer, and the deterministic failures below never improve.
struct retry_policy {
  int max_retries = 0;
  std::chrono::milliseconds interval{0};

  [[nodiscard]] bool retries(error_code code) const noexcept {
    if (max_retries <= 0) {
      return false;
    }
    switch (code) {
      case error_code::ERR_REQUEST_TRANSPORT:
      case error_code::ERR_REQUEST_STREAM:
      case error_code::ERR_INTERNAL:
        return true;
      default:
        return false;
    }
  }
};

// A `client` is either fully open or it does not exist: construction loads the
// assistant, takes its instance lock, opens its log and database, and throws
// `open_error` if any of that fails. Nothing is left half-built, so the caller
// never has to guard against a null assistant or database.
//
// Only the category travels in the exception. The detail behind a refusal (the
// pid holding the lock, the filesystem error) is logged, not carried: callers
// branch on `code()` and the UI words the sentence from it, so there is no
// free-form reason to keep in sync.
class open_error : public std::exception {
public:
  explicit open_error(error_code code) : code_(code) {}

  [[nodiscard]] error_code code() const noexcept { return code_; }

  [[nodiscard]] const char* what() const noexcept override {
    return "cannot open the assistant; see the log for the detail";
  }

private:
  error_code code_;
};


enum class message_kind : std::uint8_t {
  undefined = 0,
  user_prompt = 1,
  assistant_output = 2,
  assistant_reasoning_payload = 3,
  assistant_tool_call = 4,
  tool_result = 5,
  inline_compact = 6,
};

enum class session_kind : std::uint8_t {
  undefined = 0,
  standard = 1,
  temporary = 2,   // This session is not saved to the database, not implemented.
  standalone = 3,    // This session is saved to the database, but memory excludes it, not implemented.

};

struct tool_call_info {
  std::string tool_call_id_;
  std::string tool_name_;
};



class size_type {
public:
  using _Size_Type = uint32_t;
  size_type() : id_(0) {}

  size_type(_Size_Type id) : id_(id) {
    felis::check(id > 0,
      "the `id` should be greater than `0`, use default construction for unset case.");
  }

  size_type(size_type&& other) {
    felis::check(other.is_set(), "the `size_type` is unset.");
    id_ = other.id_;
    other.id_ = 0;
  }

  size_type(const size_type& other) {
    felis::check(other.is_set(), "the `size_type` is unset.");
    id_ = other.id_;
  }

  size_type& operator=(size_type&& other) {
    felis::check(other.is_set(), "the `size_type` is unset.");
    id_ = other.id_;
    other.id_ = 0;
    return *this;
  }

  size_type& operator=(const size_type& other) {
    felis::check(other.is_set(), "the `size_type` is unset.");
    id_ = other.id_;
    return *this;
  }

  bool is_set() const { return id_ != 0; }
  _Size_Type get() const {
    felis::check(is_set(), "the `size_type` is unset.");
    return id_;
  }

private:
  _Size_Type id_ = 0;
};



}
