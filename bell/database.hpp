#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "message.hpp"
#include "application.hpp"

#include "felis/sqlite3.hpp"


namespace felis::bell {

// One session's newest inline summary, as offered to the assistant-wide memory
// update. `session_title_` labels the block in the merge request; `text_` is the
// summary itself.
struct session_summary {
  size_type session_id_;
  std::string session_title_;
  std::string text_;
};

class database {
public:
  explicit database(std::string_view path);


  // Message
  void update_message_content(size_type id, const std::string& content) const;
  // Marks `message_id` as folded into the summary `summary_id`: it stops being
  // replayed (compacted_at != 0) and records which summary absorbed it.
  void compact_message(size_type message_id, size_type summary_id) const;
  std::vector<shadowed_message> all_not_compacted_messages(size_type session_id) const;
  // The complete conversation, compacted rows and summaries included, oldest
  // first. Backs the transcript pane so the reader can scroll back to the actual
  // messages a summary folded away; the model request instead uses the compacted
  // view (`all_not_compacted_messages`).
  std::vector<shadowed_message> all_messages(size_type session_id) const;
  size_type message_session(size_type message_id) const;
  bool remove_message(size_type message_id) const;
  std::size_t remove_messages_from(size_type session_id, size_type message_id) const;

  // Session
  void update_session(const session& session) const;
  void remove_session(size_type id) const;
  [[nodiscard]] std::optional<session> get_session(size_type id) const;
  [[nodiscard]] bool session_exists(size_type id) const;
  [[nodiscard]] std::string custom_instructions(size_type session_id) const;
  void set_custom_instructions(size_type session_id, const std::string& instructions) const;
  void set_session_reasoning_effort(size_type id, const std::string& effort) const;
  // std::optional<std::string> last_message_time(std::uint32_t session_id) const;

  // Shadowed Message
  // std::vector<message> messages_after_memory_watermark() const;
  std::vector<shadowed_message> recent_messages(size_type session_id, std::size_t count) const;

  // Memory inputs. Both read only `session_kind::standard` sessions: `temporary`
  // and `standalone` are excluded by their kind, with no per-session flag.
  //
  // The newest `inline_compact` summary of every standard session, newest first,
  // charged against `token_budget` with `felis::str::estimate_tokens` so the
  // shared memory request cannot grow without bound.
  [[nodiscard]] std::vector<session_summary> session_summaries(
    std::size_t token_budget
  ) const;

  // The newest plain conversation rows (user prompts and assistant text) across
  // every standard session, walked newest first and charged against
  // `token_budget`, then returned oldest first. Tool traffic, stored reasoning
  // payloads and summaries are excluded: memory is prose, the summaries arrive
  // through `session_summaries` already, and a call/result pair cut by the budget
  // would leave an unanswered `function_call` the provider rejects.
  [[nodiscard]] std::vector<shadowed_message> messages_across_sessions(
    std::size_t token_budget
  ) const;
  std::uint32_t insert_message(const shadowed_message& message) const;

  // Request Usage
  //
  // One row per successful HTTP request, keyed by the user message that opened
  // its turn. The raw counts are kept per request; the auto-compact predicate
  // reads only the newest one.
  void record_request_usage(
    size_type user_message_id, std::uint32_t input_tokens, std::uint32_t output_tokens
  ) const;

  // Token size of the newest request's prompt (input + output), or `std::nullopt`
  // when it is unknown or describes a summarization request.
  [[nodiscard]] std::optional<std::uint64_t> context_usage(size_type session_id) const;

  // True when the newest measured context has reached the session's threshold.
  [[nodiscard]] bool exceeds_auto_compact_threshold(size_type session_id) const;

  // Shadowed Session
  [[nodiscard]] std::uint32_t insert_session(const shadowed_session& session) const;
  [[nodiscard]] std::vector<shadowed_session> sessions_by_recent(std::size_t limit = 0) const;

  std::size_t message_count(std::uint32_t session_id) const;
  std::unordered_map<std::uint32_t, std::string> session_last_times() const;

private:
  // Utils
  session read_session(const felis::sqlite::statement& stmt) const;
  static shadowed_session read_shadowed_session(const felis::sqlite::statement& stmt);
  // message read_message(const felis::sqlite::statement& stmt) const;
  static shadowed_message read_shadowed_message(const felis::sqlite::statement& stmt);

  // Resets `compacted_at` to 0 for every message whose recorded summary no
  // longer exists. Called after a delete: removing a summary must resurface the
  // history it had folded away, not leave it invisible behind a dangling
  // `compacted_at` pointing at a row that is gone.
  void reset_orphaned_compactions() const;

private:
  felis::sqlite::connection connection_;
};

} // namespace felis::bell
