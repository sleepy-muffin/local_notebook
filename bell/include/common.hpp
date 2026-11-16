#pragma once

#include <cstdint>
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
  ERR_SESSION_SUMMARY_TRIGGER_HAS_MET = 200,
  ERR_ASSISTANT_MEMORY_TRIGGER_HAS_MET = 201,

  // Another task already owns the session (one turn or fold at a time), or the
  // shared assistant fold is running and would read this session's rows.
  ERR_SESSION_BUSY = 202,
  ERR_ASSISTANT_BUSY = 203,
  ERR_ASSISTANT_HAS_NOT_LOADED_YET = 204,
  ERR_ASSISTANT_OR_ANY_SESSION_BUSY = 205,


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
