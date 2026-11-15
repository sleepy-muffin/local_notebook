#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <felis/sync.hpp>
#include <felis/legacy.hpp>

#include "common.hpp"
#include "felis/log.hpp"
#include "store.hpp"
#include "database.hpp"
#include "application.hpp"

namespace felis::bell {

// The streaming surface of one turn. `client::prompt` drives these as the turn
// unfolds; the caller (the UI) only reacts. It runs on the caller's worker
// thread, so a GUI caller must marshal each callback onto its own event loop.
//
// Every hook is optional. `on_text` carries the visible assistant delta;
// `on_reasoning` the hidden "thinking"; `on_status` a short human status (a tool
// name, "compacting"); `on_message_begin` fires once at the start of every
// assistant message (each tool-loop iteration's model call), before that
// message's deltas, so the UI can close the previous streaming bubble; `on_done`
// fires exactly once with the terminal outcome.
struct prompt_events {
  std::function<void(std::string_view)> on_text;
  std::function<void(std::string_view)> on_reasoning;
  std::function<void(std::string_view)> on_status;
  std::function<void()> on_message_begin;
  std::function<void(bool ok, std::string_view error)> on_done;
};

struct assistant_state {

  assistant_state(
      std::string uuid, assistant assistant,
      std::unique_ptr<felis::legacy::instance_lock> filesystem_lock)
          : uuid_(std::move(uuid)), assistant_(std::move(assistant)),
            filesystem_lock_(std::move(filesystem_lock)) {}

  std::string uuid_;
  assistant assistant_;
  std::unique_ptr<felis::legacy::instance_lock> filesystem_lock_;
  mutable std::optional<bool> assistant_due_cache_;
};


class client{

public:

  client(const client&) = delete;
  client& operator=(const client&) = delete;

  client(client&&) = default;
  client& operator=(client&&) = default;

  // The constructor should be guarded by try... catch
  client(store store, const std::string& uuid): store_(std::move(store)), assistant_uuid_(uuid) {
    open_assistant();
  }

  error_code open_assistant();

  void open_log(const std::string &uuid) {
    const auto path = store_.assistant_log(uuid);
    try {
      felis::log::set_output_file(path);
    } catch (const std::exception &error) {
      felis::log::error("cannot open log file {}: {}", path.string(), error.what());
    }
    felis::log::info("===== local-notebook session started =====");
  }

  size_type create_session(const shadowed_session& session) {
    felis::check(db_ != nullptr);
    return db_->insert_session(session);
  }

  auto load_recent_messages(size_type session_id, size_t count = 5) {
    return db_->recent_messages(session_id, count);
  }

  std::string custom_instructions(size_type session_id) {
    return db_->custom_instructions(session_id);
  }

  void set_custom_instructions(size_type session_id, const std::string& instructions) {
    db_->set_custom_instructions(session_id, instructions);
  }

  // ----- Read surface for the UI -------------------------------------------
  //
  // The widgets read sessions as `shadowed_session` (id + title) and messages as
  // `shadowed_message`; only the settings editors need a full `session` row, so
  // `session()` returns it separately. All of these delegate straight to the
  // database that `client` already owns.

  [[nodiscard]] const store& get_store() const { return store_; }

  // ----- Assistant surface --------------------------------------------------
  //
  // The UI asks the store which assistants exist, edits the open one in place,
  // and reads the open assistant's settings (model, endpoint, proxy).

  [[nodiscard]] std::vector<std::string> list_assistants() const {
    return store_.scan_assistants();
  }

  [[nodiscard]] const std::string& assistant_uuid() const { return assistant_uuid_; }

  [[nodiscard]] assistant current_assistant() const {
    auto state = assistant_state_->lock();
    return state->assistant_;
  }

  // Replaces the open assistant's settings in memory and on disk. Used when the
  // picker edits the assistant that is currently open.
  void set_assistant(const assistant& value) {
    {
      auto state = assistant_state_->lock();
      state->assistant_ = value;
    }
    store_.save_assistant(assistant_uuid_, value);
  }

  [[nodiscard]] std::vector<shadowed_session> sessions(std::size_t limit = 0) {
    return db_->sessions_by_recent(limit);
  }

  [[nodiscard]] bool session_exists(size_type session_id) const {
    return db_->session_exists(session_id);
  }

  // Persists an edited session row (title, custom instructions, pin, budgets).
  void save_session(const session& value) { db_->update_session(value); }

  [[nodiscard]] std::optional<felis::bell::session> load_session(size_type session_id) {
    return db_->get_session(session_id);
  }

  // Sets the reasoning level a session's normal chat turns run at. Called from the
  // composer's reasoning selector; the inline summariser uses its own
  // `auto_compact_reasoning_effort`, set through `save_session`.
  void set_session_reasoning_effort(size_type session_id, const std::string& level) {
    db_->set_session_reasoning_effort(session_id, level);
  }

  // Stamps the owning session onto `msg` and persists it; returns the new id.
  size_type append_message(size_type session_id, const shadowed_message& msg);

  // The message range for one request: the last summary plus every message
  // after it. With `is_auto_compact`, the verbatim tail reserved by
  // `auto_compact_preserve` is withheld, since that tail survives compaction.
  std::vector<shadowed_message> request_messages(size_type session_id, bool is_auto_compact);

  // The full conversation, compacted rows included: the transcript the chat pane
  // renders. Kept separate from the model request, which only sees the summary
  // plus the live tail so it can review messages a summary has folded away.
  [[nodiscard]] std::vector<shadowed_message> messages(size_type session_id) {
    return db_->all_messages(session_id);
  }

  [[nodiscard]] std::unordered_map<std::uint32_t, std::string> session_last_times() {
    return db_->session_last_times();
  }

  [[nodiscard]] std::size_t message_count(size_type session_id) {
    return db_->message_count(session_id.get());
  }

  void edit_message(size_type message_id, const std::string& content) {
    db_->update_message_content(message_id, content);
  }

  void delete_message(size_type message_id) {
    db_->remove_message(message_id);
  }

  // Deletes `message_id` and every newer message of its session; returns the
  // number removed.
  std::size_t delete_messages_from(size_type session_id, size_type message_id) {
    return db_->remove_messages_from(session_id, message_id);
  }

  void delete_session(size_type session_id) {
    db_->remove_session(session_id);
  }

  [[nodiscard]] std::optional<std::uint64_t> context_usage(size_type session_id) const {
    return db_->context_usage(session_id);
  }

  // Persists the user's turn synchronously and returns the new message id. A GUI
  // caller runs this on its own thread so the user's own message is committed
  // before it rebuilds the transcript — otherwise the rebuild races the worker's
  // insert and the prompt appears only after the turn finishes.
  size_type begin_turn(size_type session_id, const std::string& prompt_text);

  // Drives one turn to completion: the user prompt (already persisted by
  // `begin_turn`, whose id is `user_message_id`) is the per-turn key, then loops
  // (request -> persist reply -> run any tools) until the model returns no tool
  // call. Auto-compaction runs at the head of each iteration. The whole turn
  // streams through `events`; it blocks the calling thread, so run it on a worker.
  //
  // `drain_steering` (optional) is how a prompt typed while the turn is running
  // joins it: the caller persists that prompt on its own thread and then arms a
  // flag this callback consumes, returning the newest steered message id (0 when
  // nothing is pending). It is called on the worker thread when the model stops
  // calling tools; an id newer than anything in the request just sent means the
  // steer missed that request and the loop must run once more. No text crosses
  // this boundary — the database is the channel, and `request_messages` re-reads
  // it every iteration.
  void prompt(
    std::stop_token stop,
    size_type session_id,
    size_type user_message_id,
    const prompt_events& events,
    const std::function<std::uint32_t()>& drain_steering = {}
  );

  // Merges the assistant's memory from two pools: each standard session's newest
  // summary (`memory_budget_for_session_compact_`) and the newest conversation
  // rows across those sessions (`memory_budget_across_messages_`). The reply is
  // capped by `memory_budget_` and written straight back to the assistant.
  //
  // Deliberately separate from `prompt`: this is manual and assistant-wide, never
  // auto-triggered, and it is not part of any session's turn. It blocks the
  // calling thread, so run it on a worker. Returns false when both pools are
  // empty or the request produced no usable text; the memory is left untouched.
  bool update_memory(std::stop_token stop, const prompt_events& events);

private:
  store store_;
  std::string assistant_uuid_;
  // std::unique_ptr<assistant> assistant_ = nullptr;
  std::unique_ptr<felis::synchronized<assistant_state>> assistant_state_ = nullptr;

  std::unique_ptr<database> db_ = nullptr;
};

}
