#include "ui/ConversationController.hpp"

#include <algorithm>
#include <utility>

namespace ui {

ConversationController::ConversationController(
    felis::bell::client& client, felis::async::multi_executor<void()>& executor,
    std::uint32_t session_id
)
    : client_(client), executor_(executor), session_id_(session_id) {}

ConversationController::~ConversationController() {
  // Ask the turn to wind down, but never block the GUI thread waiting for it. The
  // worker's callbacks capture `state_` (not `this`), so a push after this object
  // is gone lands in live shared memory and is simply never drained.
  handle_.request_stop();
}

void ConversationController::push(event_kind kind, std::string_view text) {
  std::scoped_lock lock(state_->mutex);
  state_->events.push_back(event{kind, std::string(text)});
}

bool ConversationController::send(std::string text) {
  if (busy() || text.empty()) {
    return false;
  }

  const felis::bell::size_type sid(session_id_);
  const std::shared_ptr<shared_state> state = state_;

  // Persist the user's turn now, on the GUI thread, so the rebuild just below
  // shows it immediately instead of racing the worker's insert (which is what
  // made the prompt appear only after the turn finished).
  const felis::bell::size_type user_message_id = client_.begin_turn(sid, text);

  // The turn runs on the shared executor. The callbacks only enqueue; nothing in
  // them touches a widget, so they are safe on the worker thread.
  felis::bell::prompt_events events;
  events.on_text = [state](std::string_view delta) {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(event{event_kind::text, std::string(delta)});
  };
  events.on_reasoning = [state](std::string_view delta) {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(event{event_kind::reasoning, std::string(delta)});
  };
  events.on_status = [state](std::string_view delta) {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(event{event_kind::status, std::string(delta)});
  };
  events.on_message_begin = [state]() {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(event{event_kind::message_begin, std::string()});
  };
  events.on_rows_appended = [state]() {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(event{event_kind::rows_appended, std::string()});
  };
  events.on_steered = [state](felis::bell::size_type id) {
    std::scoped_lock lock(state->mutex);
    event item{event_kind::steered, std::string()};
    item.id = id.get();
    state->events.push_back(std::move(item));
  };
  events.on_done = [state](bool ok, std::string_view error) {
    std::scoped_lock lock(state->mutex);
    state->events.push_back(
        event{ok ? event_kind::done_ok : event_kind::done_error, std::string(error)}
    );
  };

  // Capture a pointer to the (application-owned, longer-lived) client rather than
  // `this`, so the worker never dereferences a destroyed controller.
  felis::bell::client* client = &client_;

  handle_ = executor_.spawn(
      [client, sid, user_message_id, events, state](std::stop_token stop) mutable {
        // The worker drains the queued prompts at each request boundary, so they
        // never interrupt a reply that is still streaming.
        const std::function<std::vector<std::string>()> take = [state]() {
          std::scoped_lock lock(state->mutex);
          std::vector<std::string> pending;
          pending.reserve(state->steered.size());
          for (steer_entry& entry : state->steered) {
            pending.push_back(std::move(entry.text));
          }
          state->steered.clear();
          return pending;
        };

        client->prompt(stop, sid, user_message_id, events, take);
      }
  );

  // Rebuild the view now so the user's own message appears; the done event
  // triggers another rebuild once the turn is stored.
  if (on_changed_) {
    on_changed_();
  }

  return true;
}

void ConversationController::cancel() { handle_.request_stop(); }

bool ConversationController::steer(std::string text) {
  if (!busy() || text.empty()) {
    return false;
  }

  // Only the text crosses to the worker. Storing the row here would append it
  // before the reply it interrupts (the reply is only persisted at end of
  // stream), which would reorder the transcript.
  {
    std::scoped_lock lock(state_->mutex);
    state_->steered.push_back(steer_entry{next_steer_serial_++, std::move(text)});
  }

  return true;
}

std::vector<ConversationController::steer_entry> ConversationController::queued_steers() const {
  std::scoped_lock lock(state_->mutex);
  return state_->steered;
}

bool ConversationController::remove_steer(std::uint64_t serial) {
  std::scoped_lock lock(state_->mutex);

  const auto it = std::find_if(
      state_->steered.begin(), state_->steered.end(),
      [serial](const steer_entry& entry) { return entry.serial == serial; });

  if (it == state_->steered.end()) {
    return false;
  }

  state_->steered.erase(it);
  return true;
}

void ConversationController::delete_message_only(std::uint32_t message_id) {
  client_.delete_message(felis::bell::size_type(message_id));

  if (on_changed_) {
    on_changed_();
  }
}

void ConversationController::delete_message(std::uint32_t message_id) {
  client_.delete_messages_from(
      felis::bell::size_type(session_id_), felis::bell::size_type(message_id));

  if (on_changed_) {
    on_changed_();
  }
}

void ConversationController::poll() {
  std::vector<event> pending;
  {
    std::scoped_lock lock(state_->mutex);
    pending.swap(state_->events);
  }

  for (const event& item: pending) {
    switch (item.kind) {
      case event_kind::text:
        if (on_stream_) {
          on_stream_(item.text);
        }
        break;
      case event_kind::reasoning:
        if (on_reasoning_) {
          on_reasoning_(item.text);
        }
        break;
      case event_kind::status:
        if (on_status_) {
          on_status_(item.text);
        }
        break;
      case event_kind::message_begin:
        // A new assistant message is starting: rebuild the transcript from the DB
        // so the previous message stays a separate bubble (its text is already
        // persisted) and the incoming deltas begin a fresh streaming bubble.
        if (on_changed_) {
          on_changed_();
        }
        break;
      case event_kind::rows_appended:
        // The worker stored a row (a tool call before it runs, or a result just
        // after): rebuild so the strip appears while the work is still in
        // flight. Anything already streamed for this request is persisted by
        // now, so the reload cannot drop it.
        if (on_changed_) {
          on_changed_();
        }
        break;
      case event_kind::steered:
        // The worker stored the steered prompt; mark it, then reload so its
        // bubble appears in place.
        if (on_steered_) {
          on_steered_(item.id);
        }
        if (on_changed_) {
          on_changed_();
        }
        break;
      case event_kind::done_ok:
        if (on_changed_) {
          on_changed_();
        }
        break;
      case event_kind::done_error:
        if (!item.text.empty() && on_error_) {
          on_error_(item.text);
        }
        if (on_changed_) {
          on_changed_();
        }
        break;
    }
  }
}

} // namespace ui
