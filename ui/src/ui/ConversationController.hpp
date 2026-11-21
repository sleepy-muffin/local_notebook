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

  // Steers the in-flight turn with a prompt typed while it runs. Only the text
  // travels to the worker: it waits in the queue until the current request has
  // finished, then the worker stores it after that request's rows (so the
  // transcript's id order stays truthful) and runs one more iteration so the
  // model answers it in this same turn. A steer never cuts the streaming reply
  // short. Returns false when no turn is in flight (the caller should `send`
  // instead) or the text is empty.
  [[nodiscard]] bool steer(std::string text);

  // A prompt typed while the turn runs and not yet delivered to it. `serial` is
  // its identity for removal: the worker drains the whole queue at once, so an
  // index would be ambiguous the moment a drain races a click.
  struct steer_entry {
    std::uint64_t serial = 0;
    std::string text;
  };

  // Snapshot of the queue, in the order the prompts will be delivered.
  [[nodiscard]] std::vector<steer_entry> queued_steers() const;

  // Drops one queued prompt. Returns false when the worker already took it.
  bool remove_steer(std::uint64_t serial);

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
  // Reports the id of a row the worker stored for a steered prompt, so the view
  // can mark it and reload the transcript.
  void set_on_steered(std::function<void(std::uint32_t)> handler) {
    on_steered_ = std::move(handler);
  }

  // Supplies the retry policy for a turn. Read when `send` starts the turn, so a
  // settings change applies to the next send without reopening the view. An
  // unset provider means no retrying.
  void set_retry_policy(std::function<felis::bell::retry_policy()> provider) {
    retry_policy_ = std::move(provider);
  }

  // Drains the worker's queued events and dispatches them on the calling (GUI)
  // thread. Call from the window's poll timer.
  void poll();

private:
  enum class event_kind {
    text,
    reasoning,
    status,
    message_begin,
    rows_appended,
    steered,
    done_ok,
    done_error
  };

  struct event {
    event_kind kind;
    std::string text;
    // The message id, for `steered`.
    std::uint32_t id = 0;
  };

  // Shared with the worker's event lambdas so a push after this controller is
  // destroyed still lands in live memory (the worker may outlive the widget).
  struct shared_state {
    std::mutex mutex;
    std::vector<event> events;

    // Prompts typed while the turn runs, in order. The worker takes them through
    // `take_steering` between iterations; see `client::prompt`.
    std::vector<steer_entry> steered;
  };

  void push(event_kind kind, std::string_view text);

  felis::bell::client& client_;
  felis::async::multi_executor<void()>& executor_;
  std::uint32_t session_id_;

  std::shared_ptr<shared_state> state_ = std::make_shared<shared_state>();

  // Identity for queued steers. Only touched on the GUI thread.
  std::uint64_t next_steer_serial_ = 1;

  felis::async::multi_executor<void()>::handle handle_;

  std::function<void()> on_changed_;
  std::function<void(const std::string&)> on_stream_;
  std::function<void(const std::string&)> on_reasoning_;
  std::function<void(const std::string&)> on_status_;
  std::function<void(const std::string&)> on_error_;
  std::function<void(std::uint32_t)> on_steered_;
  std::function<felis::bell::retry_policy()> retry_policy_;
};

} // namespace ui
