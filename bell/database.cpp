#include "database.hpp"
#include "application.hpp"
#include "felis/sqlite3.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <felis/json.hpp>

namespace {

constexpr std::string_view SCHEMA = R"sql(
PRAGMA user_version = 16;

CREATE TABLE IF NOT EXISTS session (
  id                                INTEGER PRIMARY KEY AUTOINCREMENT,
  title                             TEXT    NOT NULL DEFAULT '',
  custom_instructions               TEXT    NOT NULL DEFAULT '',
  shared_directories                TEXT    NOT NULL DEFAULT '[]',
  pinned                            INTEGER NOT NULL DEFAULT 0,

  auto_compact_threshold            INTEGER NOT NULL DEFAULT 16000,
  auto_compact_preserve             INTEGER NOT NULL DEFAULT 4000,
  auto_compact_budget               INTEGER NOT NULL DEFAULT 4000,

  input_token_produced_midway       INTEGER NOT NULL DEFAULT 32000,
  response_budget                   INTEGER NOT NULL DEFAULT 8000,
  reasoning_effort                  TEXT    NOT NULL DEFAULT 'none',
  auto_compact_reasoning_effort          TEXT    NOT NULL DEFAULT 'none',

  tools                             INTEGER NOT NULL DEFAULT 0,
  kind                              INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS message (
  id                  INTEGER  PRIMARY KEY AUTOINCREMENT,
  session_id          INTEGER  NOT NULL REFERENCES session(id) ON DELETE CASCADE,
  content             TEXT     NOT NULL,
  made_at             DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  compacted_at       INTEGER  NOT NULL DEFAULT 0,
  kind                INTEGER  NOT NULL,
  tool_call_id        TEXT     DEFAULT NULL,
  tool_name           TEXT     DEFAULT NULL,
  reasoning_payload   TEXT     DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_message_session ON message(session_id, id);
CREATE INDEX IF NOT EXISTS idx_message_made_at ON message(made_at);

-- One row per HTTP request. A turn (opened by one user message) spans 1..N such
-- rows; `user_message_id` is that turn's opening message, mirroring Zed's
-- per-turn keying while keeping each request's raw usage. Neither `made_at` nor
-- `session_id` is stored here: the paired message already carries both, and
-- per-request order is `id` order. A summarization request is inferred by pairing
-- with the `inline_summary` message it produced, so no separate marker state is
-- needed.
CREATE TABLE IF NOT EXISTS request_usage (
  id                INTEGER  PRIMARY KEY AUTOINCREMENT,
  user_message_id   INTEGER  NOT NULL REFERENCES message(id) ON DELETE CASCADE,
  input_tokens      INTEGER  NOT NULL DEFAULT 0,
  output_tokens     INTEGER  NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_request_usage_turn ON request_usage(user_message_id, id);
)sql";

// Column order here must match `database_access::read_session`.
constexpr std::string_view SESSION_COLUMNS =
  "id, title, custom_instructions, shared_directories, pinned, "
  "auto_compact_threshold, auto_compact_preserve, auto_compact_budget, "
  "response_budget, reasoning_effort, tools, kind, auto_compact_reasoning_effort";

// Column order here must match `database_access::read_message`.
constexpr std::string_view MESSAGE_COLUMNS =
  "id, session_id, content, made_at, compacted_at, kind, tool_call_id, "
  "tool_name, reasoning_payload";

// Shared directories are stored as a JSON array in one column. A row written
// before the list existed holds a single plain path, which is still accepted.
std::string encode_shared_directories(const std::vector<std::string>& directories) {
  felis::json::array array;

  for (const std::string& directory: directories) {
    array.push_back(felis::json::value(directory));
  }

  return felis::json::dumps(felis::json::value(std::move(array)));
}

std::vector<std::string> decode_shared_directories(
    const std::optional<std::string>& stored
) {
  std::vector<std::string> directories;

  if (!stored.has_value() || stored->empty()) {
    return directories;
  }

  try {
    const felis::json::value parsed = felis::json::parse(*stored);

    if (parsed.is_array()) {
      const felis::json::array& entries = parsed.as_array();
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const felis::json::value& entry = entries[i];
        if (entry.is_string() && !entry.as_string().empty()) {
          directories.push_back(entry.as_string());
        }
      }
      return directories;
    }
  } catch (...) {
    // Not valid JSON: fall through to the legacy single-path interpretation.
  }

  directories.push_back(*stored);
  return directories;
}

} // namespace



felis::bell::session felis::bell::database::read_session(const felis::sqlite::statement& stmt) const {
  return felis::bell::session(
    size_type(stmt.column_uint32(0)),
    stmt.column_text(1),
    stmt.column_text(2),
    stmt.column_text(9),
    stmt.column_text(12),
    decode_shared_directories(stmt.column_text_opt(3)),
    static_cast<session_kind>(stmt.column_int(11)),
    stmt.column_int(4) != 0,
    static_cast<tool_capabilities>(stmt.column_int(10)),
    size_type(stmt.column_uint32(5)),
    size_type(stmt.column_uint32(6)),
    size_type(stmt.column_uint32(7)),
    size_type(stmt.column_uint32(8))
  );
}

felis::bell::shadowed_session felis::bell::database::read_shadowed_session(const felis::sqlite::statement& stmt) {
  return shadowed_session{
    stmt.column_uint32(0),
    stmt.column_text(1),
    stmt.column_int(4) != 0
  };
}

felis::bell::shadowed_message felis::bell::database::read_shadowed_message(const felis::sqlite::statement& stmt) {
  shadowed_message message(
    stmt.column_uint32(0),
    stmt.column_text(2),
    static_cast<message_kind>(stmt.column_int(5))
  );
  message.session_id_ = stmt.column_uint32(1);
  message.made_at_ = stmt.column_text(3);
  message.reasoning_payload_ = stmt.column_text_opt(8);

  const std::string tool_call_id = stmt.column_text(6);
  const std::string tool_name = stmt.column_text(7);
  if (!tool_call_id.empty() || !tool_name.empty()) {
    message.tool_call_ = tool_call_info{tool_call_id, tool_name};
  }

  return message;
}

// felis::bell::shadowed_message felis::bell::database::read_message(const felis::sqlite::statement& stmt
// ) const {
//   return felis::bell::message(
//     *this,
//     size_type(stmt.column_uint32(0)),
//     size_type(stmt.column_uint32(1)),
//     static_cast<message_kind>(stmt.column_int(5)),
//     stmt.column_text(2),
//     stmt.column_text(3),
//     stmt.column_uint32(4),
//     stmt.column_text(6),
//     stmt.column_text(7),
//     stmt.column_text_opt(8)
//   );
// }

felis::bell::database::database(std::string_view path) : connection_(path) {
  connection_.exec(SCHEMA);

  // Older database files predate `auto_compact_reasoning_effort`. `CREATE TABLE IF NOT
  // EXISTS` in SCHEMA never re-shapes an existing table, so add the column to a
  // file that lacks it. Probe via PRAGMA first (the statement is scoped so it
  // releases the connection lock before the ALTER's exec), and only migrate when
  // the column is genuinely missing.
  bool has_auto_compact_reasoning_effort = false;
  {
    auto stmt = connection_.prepare("PRAGMA table_info(session)");
    while (stmt.step()) {
      if (stmt.column_text(1) == "auto_compact_reasoning_effort") {
        has_auto_compact_reasoning_effort = true;
        break;
      }
    }
  }
  if (!has_auto_compact_reasoning_effort) {
    connection_.exec(
      "ALTER TABLE session ADD COLUMN auto_compact_reasoning_effort TEXT NOT NULL DEFAULT 'none'"
    );
  }
}

std::uint32_t felis::bell::database::insert_session(const felis::bell::shadowed_session& session) const {
  auto stmt = connection_.prepare(
    "INSERT INTO session (title) VALUES (?)"
  );
  stmt.bind(session.title_);
  return stmt.execute_with_last_row_id();
}

void felis::bell::database::update_session(const felis::bell::session& session) const {
  auto stmt = connection_.prepare(
    "UPDATE session SET title = ?, custom_instructions = ?, shared_directories = ?, "
    "pinned = ?, auto_compact_threshold = ?, auto_compact_preserve = ?, "
    "auto_compact_budget = ?, response_budget = ?, reasoning_effort = ?, "
    "auto_compact_reasoning_effort = ?, tools = ?, kind = ? WHERE id = ?"
  );
  stmt.bind(
    session.title(), session.custom_instructions(),
    encode_shared_directories(session.shared_directories()), session.pinned() ? 1 : 0,
    static_cast<int>(session.auto_compact_threshold().get()),
    static_cast<int>(session.auto_compact_preserve().get()),
    static_cast<int>(session.auto_compact_budget().get()),
    static_cast<int>(session.response_budget().get()), session.reasoning_effort(),
    session.auto_compact_reasoning_effort(),
    static_cast<int>(session.tools().raw()), static_cast<int>(session.kind()),
    session.id().get()
  );
  stmt.execute();
}

void felis::bell::database::remove_session(size_type id) const {
  auto stmt = connection_.prepare("DELETE FROM session WHERE id = ?");
  stmt.bind(id.get());
  stmt.execute();
}

std::optional<::felis::bell::session> felis::bell::database::get_session(
  size_type id
) const {
  auto stmt = connection_.prepare(
    "SELECT " + std::string(SESSION_COLUMNS) + " FROM session WHERE id = ?"
  );
  stmt.bind(id.get());
  if (stmt.step()) {
    return read_session(stmt);
  }
  return std::nullopt;
}

bool felis::bell::database::session_exists(size_type id) const {
  auto stmt = connection_.prepare("SELECT 1 FROM session WHERE id = ?");
  stmt.bind(id.get());
  return stmt.step();
}

std::string felis::bell::database::custom_instructions(size_type session_id) const {
  auto stmt = connection_.prepare(
    "SELECT custom_instructions FROM session WHERE id = ?"
  );
  stmt.bind(session_id.get());
  if (stmt.step()) {
    return stmt.column_text(0);
  }
  return {};
}

void felis::bell::database::set_custom_instructions(
  size_type session_id, const std::string& instructions
) const {
  auto stmt = connection_.prepare(
    "UPDATE session SET custom_instructions = ? WHERE id = ?"
  );
  stmt.bind(instructions, session_id.get());
  stmt.execute();
}

std::vector<felis::bell::shadowed_session> felis::bell::database::sessions_by_recent(std::size_t limit) const {
  std::string sql =
    "SELECT " + std::string(SESSION_COLUMNS) +
    ", (SELECT MAX(made_at) FROM message WHERE message.session_id = session.id) AS last_at "
    "FROM session ORDER BY pinned DESC, last_at IS NULL, last_at DESC, id DESC";
  if (limit > 0) {
    sql += " LIMIT ?";
  }

  auto stmt = connection_.prepare(sql);
  if (limit > 0) {
    stmt.bind(static_cast<int>(limit));
  }

  std::vector<shadowed_session> shadows;
  while (stmt.step()) {
    shadows.push_back(read_shadowed_session(stmt));
  }
  return shadows;
}

void felis::bell::database::set_session_reasoning_effort(size_type id, const std::string& effort) const {
  auto stmt = connection_.prepare(
    "UPDATE session SET reasoning_effort = ? WHERE id = ?"
  );
  stmt.bind(effort, id.get());
  stmt.execute();
}

std::uint32_t felis::bell::database::insert_message(const shadowed_message& message) const {
  const std::string content = message.content_;
  const std::optional<std::string> reasoning_payload = message.reasoning_payload_;

  // Only tool calls and their results carry a call identity; a plain prompt or
  // assistant text has none, so those two columns stay NULL rather than empty.
  const std::optional<std::string> tool_call_id =
    message.tool_call_ ? std::optional<std::string>(message.tool_call_->tool_call_id_) : std::nullopt;
  const std::optional<std::string> tool_name =
    message.tool_call_ ? std::optional<std::string>(message.tool_call_->tool_name_) : std::nullopt;

  auto stmt = connection_.prepare(
    "INSERT INTO message "
    "(session_id, content, kind, tool_call_id, tool_name, reasoning_payload) "
    "VALUES (?, ?, ?, ?, ?, ?)"
  );
  stmt.bind(
    message.session_id_, content,
    static_cast<int>(message.kind_), tool_call_id, tool_name,
    reasoning_payload
  );
  return stmt.execute_with_last_row_id();
}

void felis::bell::database::update_message_content(size_type id, const std::string& content) const {
  auto stmt = connection_.prepare("UPDATE message SET content = ? WHERE id = ?");
  stmt.bind(content, id.get());
  stmt.execute();
}

void felis::bell::database::compact_message(
  size_type message_id, size_type summary_id
) const {
  auto stmt = connection_.prepare("UPDATE message SET compacted_at = ? WHERE id = ?");
  stmt.bind(summary_id.get(), message_id.get());
  stmt.execute();
}

bool felis::bell::database::remove_message(size_type message_id) const {
  bool removed = false;
  {
    // `statement` holds the connection mutex for its whole lifetime, so it must
    // be destroyed before the cleanup query below tries to take the same lock.
    auto stmt = connection_.prepare("DELETE FROM message WHERE id = ?");
    stmt.bind(message_id.get());
    stmt.execute();
    removed = stmt.changes() > 0;
  }
  if (removed) {
    // A deleted summary must not keep the messages it absorbed hidden.
    reset_orphaned_compactions();
  }
  return removed;
}

std::size_t felis::bell::database::remove_messages_from(
  size_type session_id, size_type message_id
) const {
  std::size_t removed = 0;
  {
    // Release the connection lock before the cleanup query below re-locks it.
    auto stmt = connection_.prepare(
      "DELETE FROM message WHERE session_id = ? AND id >= ?"
    );
    stmt.bind(session_id.get(), message_id.get());
    stmt.execute();
    removed = static_cast<std::size_t>(stmt.changes());
  }
  if (removed > 0) {
    // The removed range may include a summary whose absorbed messages sit just
    // before it and survived the cut; resurface them rather than leave orphans.
    reset_orphaned_compactions();
  }
  return removed;
}

void felis::bell::database::reset_orphaned_compactions() const {
  // `compacted_at` only ever points at a summary row; if that row is gone the
  // messages it absorbed would otherwise stay hidden forever (the transcript and
  // model request both read `compacted_at = 0`). Reset them so removing a
  // summary simply removes the summary and brings the underlying history back.
  auto stmt = connection_.prepare(
    "UPDATE message SET compacted_at = 0 "
    "WHERE compacted_at != 0 AND compacted_at NOT IN (SELECT id FROM message)"
  );
  stmt.execute();
}

felis::bell::size_type felis::bell::database::message_session(
  size_type message_id
) const {
  auto stmt = connection_.prepare("SELECT session_id FROM message WHERE id = ?");
  stmt.bind(message_id.get());

  if (stmt.step()) {
    return stmt.column_uint32(0);
  }

  return {};
}

std::vector<felis::bell::shadowed_message> felis::bell::database::all_not_compacted_messages(
  size_type session_id
) const {
  // A message is visible when it was never compacted, or when the summary that
  // absorbed it no longer exists. A summary is an ordinary row that can be
  // deleted on its own; deleting it must not silently erase the history it had
  // folded away, which is what a bare `compacted_at = 0` filter would do (the
  // absorbed messages keep a dangling compacted_at pointing at the missing
  // summary and never surface again). Resurfacing orphans also keeps the model
  // request honest: with no summary left, those messages are the only record of
  // that stretch of conversation.
  auto stmt = connection_.prepare(
    "SELECT " + std::string(MESSAGE_COLUMNS) +
    " FROM message WHERE session_id = ? AND (compacted_at = 0 OR compacted_at "
    "NOT IN (SELECT id FROM message WHERE session_id = ?)) "
    "ORDER BY id ASC"
  );
  // Both placeholders must be bound in a single `bind(...)` call: each `bind()`
  // builds a fresh `sqlite_binder` whose parameter counter starts at 1, so a
  // second `stmt.bind(x)` would overwrite `?1` and leave `?2` NULL. A NULL
  // `?2` makes the subquery empty, turning `compacted_at NOT IN ()` true for
  // every row and resurfacing all compacted messages.
  stmt.bind(session_id.get(), session_id.get());

  std::vector<shadowed_message> messages;
  while (stmt.step()) {
    messages.push_back(read_shadowed_message(stmt));
  }
  return messages;
}

std::vector<felis::bell::shadowed_message> felis::bell::database::all_messages(
  size_type session_id
) const {
  // No `compacted_at` filter: this is the full transcript, including the rows a
  // summary absorbed. The model request must NOT see these (it uses
  // `all_not_compacted_messages`), but the chat pane should let the reader review
  // the real conversation behind a summary rather than only the summary itself.
  auto stmt = connection_.prepare(
    "SELECT " + std::string(MESSAGE_COLUMNS) +
    " FROM message WHERE session_id = ? ORDER BY id ASC"
  );
  stmt.bind(session_id.get());

  std::vector<shadowed_message> messages;
  while (stmt.step()) {
    messages.push_back(read_shadowed_message(stmt));
  }
  return messages;
}

// std::vector<felis::bell::message> felis::bell::database::messages_after_memory_watermark() const {
//   // The watermark lives on each session row, so the join pairs every message
//   // with its own session's frontier. Ids are global to the assistant database,
//   // which makes the comparison and the ordering both meaningful.
//   auto stmt = connection_.prepare(
//     "SELECT m.id, m.session_id, m.content, m.made_at, m.compacted_at, m.kind, "
//     "m.tool_call_id, m.tool_name, m.reasoning_payload, m.tokens "
//     "FROM message m JOIN session s ON s.id = m.session_id "
//     "WHERE m.id > s.assistant_memory_watermark "
//     "AND s.include_in_assistant_memory != 0 ORDER BY m.id ASC"
//   );
//
//   std::vector<message> messages;
//   while (stmt.step()) {
//     messages.push_back(read_message(stmt));
//   }
//   return messages;
// }

std::vector<felis::bell::shadowed_message> felis::bell::database::recent_messages(
  size_type session_id, std::size_t count
) const {
  auto stmt = connection_.prepare(
    "SELECT " + std::string(MESSAGE_COLUMNS) +
    " FROM message WHERE session_id = ? ORDER BY id DESC LIMIT ?"
  );
  stmt.bind(session_id.get(), static_cast<int>(count));

  std::vector<shadowed_message> messages;
  while (stmt.step()) {
    messages.push_back(read_shadowed_message(stmt));
  }
  std::reverse(messages.begin(), messages.end());
  return messages;
}

std::size_t felis::bell::database::message_count(std::uint32_t session_id) const {
  auto stmt = connection_.prepare(
    "SELECT COUNT(*) FROM message WHERE session_id = ?"
  );
  stmt.bind(session_id);
  if (stmt.step()) {
    return static_cast<std::size_t>(stmt.column_int(0));
  }
  return 0;
}

// std::optional<std::string> felis::bell::database::last_message_time(
//   std::uint32_t session_id
// ) const {
//   auto stmt = connection_.prepare(
//     "SELECT MAX(made_at) FROM message WHERE session_id = ?"
//   );
//   stmt.bind(session_id);
//   if (stmt.step()) {
//     return stmt.column_text_opt(0);
//   }
//   return std::nullopt;
// }

void felis::bell::database::record_request_usage(
  size_type user_message_id, std::uint32_t input_tokens, std::uint32_t output_tokens
) const {
  auto stmt = connection_.prepare(
    "INSERT INTO request_usage (user_message_id, input_tokens, output_tokens) "
    "VALUES (?, ?, ?)"
  );
  stmt.bind(user_message_id.get(), input_tokens, output_tokens);
  stmt.execute();
}

std::optional<std::uint64_t> felis::bell::database::context_usage(
  size_type session_id
) const {
  auto stmt = connection_.prepare(
    "SELECT ru.input_tokens, ru.output_tokens, m.kind FROM request_usage ru "
    "JOIN message m ON m.id = ru.user_message_id "
    "WHERE m.session_id = ? ORDER BY ru.id DESC LIMIT 1"
  );
  stmt.bind(session_id.get());

  if (!stmt.step()) {
    return std::nullopt;
  }

  // A summarization request's usage describes the pre-compaction prompt, not the
  // conversation we would send next. Mirrors Zed's `context_usage_from_history`,
  // which yields None once a Compaction marker is newer than the newest measured
  // turn; here the row is simply paired with the `inline_summary` it produced, so
  // it must not be read as the current context size.
  if (static_cast<message_kind>(stmt.column_int(2)) == message_kind::inline_compact) {
    return std::nullopt;
  }

  // `output_tokens` is included because the assistant's reply is re-sent as part
  // of the next prompt, exactly as Zed's `total_input_tokens(...) + output_tokens`.
  // Cached tokens are a subset of `input_tokens` in this provider's shape, so no
  // separate cache field is added.
  return std::uint64_t(stmt.column_uint32(0)) + std::uint64_t(stmt.column_uint32(1));
}

bool felis::bell::database::exceeds_auto_compact_threshold(size_type session_id) const {
  const std::optional<std::uint64_t> usage = context_usage(session_id);
  if (!usage.has_value()) {
    return false;
  }

  auto stmt = connection_.prepare(
    "SELECT auto_compact_threshold FROM session WHERE id = ?"
  );
  stmt.bind(session_id.get());
  if (!stmt.step()) {
    return false;
  }

  return *usage >= std::uint64_t(stmt.column_uint32(0));
}

std::unordered_map<std::uint32_t, std::string> felis::bell::database::session_last_times() const {
  auto stmt = connection_.prepare(
    "SELECT session_id, MAX(made_at) FROM message GROUP BY session_id"
  );

  std::unordered_map<std::uint32_t, std::string> times;
  while (stmt.step()) {
    const std::optional<std::string> made_at = stmt.column_text_opt(1);
    if (made_at.has_value()) {
      times.emplace(stmt.column_uint32(0), *made_at);
    }
  }
  return times;
}
