#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <felis/async.hpp>

#include "client.hpp"

namespace ui {

// One session's conversation logic. The view is passive: it forwards intent
// (send, cancel, delete) and this controller drives a single blocking
// `felis::bell::client::prompt` turn on the application's shared executor. The
// turn's stream callbacks are captured into a thread-safe queue that `poll()`
// drains on the GUI thread, so the worker never touches a widget.
//
// The controller never creates a thread of its own: the application owns one
// `multi_executor<void()>` and every controller spawns onto it.
class ConversationController {
public:
  ConversationController(
      felis::bell::client& client,
      felis::async::multi_executor<void()>& executor,
      std::uint32_t session_id
  );
  ~ConversationController();

  ConversationController(const ConversationController&) = delete;
  ConversationController& operator=(const ConversationController&) = delete;

  [[nodiscard]] std::uint32_t session_id() const { return session_id_; }

  // True while the session's turn is in flight.
  [[nodiscard]] bool busy() const { return !handle_.finished(); }

  // Persists the user message and starts the turn on the shared executor. The
  // send is refused (nothing is appended) when the turn is busy or the text is
  // empty. Returns whether a turn was started.
  [[nodiscard]] bool send(std::string text);

  // Requests the in-flight turn to stop. The partial reply is discarded by bell.
  void cancel();

  // Removes only this one message; the messages after it stay.
  void delete_message_only(std::uint32_t message_id);

  // Removes this message and every message after it in the session.
  void delete_message(std::uint32_t message_id);

  void set_on_changed(std::function<void()> handler) {
    on_changed_ = std::move(handler);
  }
  void set_on_stream(std::function<void(const std::string&)> handler) {
    on_stream_ = std::move(handler);
  }
  void set_on_reasoning(std::function<void(const std::string&)> handler) {
    on_reasoning_ = std::move(handler);
  }
  void set_on_status(std::function<void(const std::string&)> handler) {
    on_status_ = std::move(handler);
  }
  void set_on_error(std::function<void(const std::string&)> handler) {
    on_error_ = std::move(handler);
  }

  // Drains the worker's queued events and dispatches them on the calling (GUI)
  // thread. Call from the window's poll timer.
  void poll();

private:
  enum class event_kind { text, reasoning, status, message_begin, done_ok, done_error };

  struct event {
    event_kind kind;
    std::string text;
  };

  // Shared with the worker's event lambdas so a push after this controller is
  // destroyed still lands in live memory (the worker may outlive the widget).
  struct shared_state {
    std::mutex mutex;
    std::vector<event> events;
  };

  void push(event_kind kind, std::string_view text);

  felis::bell::client& client_;
  felis::async::multi_executor<void()>& executor_;
  std::uint32_t session_id_;

  std::shared_ptr<shared_state> state_ = std::make_shared<shared_state>();

  felis::async::multi_executor<void()>::handle handle_;

  std::function<void()> on_changed_;
  std::function<void(const std::string&)> on_stream_;
  std::function<void(const std::string&)> on_reasoning_;
  std::function<void(const std::string&)> on_status_;
  std::function<void(const std::string&)> on_error_;
};

} // namespace ui
