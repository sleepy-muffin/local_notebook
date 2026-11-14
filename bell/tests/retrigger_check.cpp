// Replays the client::prompt() compaction sequence at the database level to check
// whether auto-compaction re-triggers immediately after a successful compaction.
#include <cstdint>
#include <cstdio>
#include <string>

#include "database.hpp"
#include "message.hpp"

using namespace felis::bell;

int main() {
  const std::string path = "/tmp/retrigger_check.db";
  std::remove(path.c_str());
  database db(path);

  // Defaults: auto_compact_threshold=16000, auto_compact_preserve=4000.
  const size_type sid(db.insert_session(shadowed_session(0, "test")));

  auto append = [&](shadowed_message m) -> size_type {
    m.session_id_ = sid.get();
    return size_type(db.insert_message(m));
  };
  // ~big chunk of "old" history: 40000 ascii chars ~ 10000 tok each, several of them
  const std::string big(40000, 'x');
  std::vector<size_type> old_ids;
  for (int i = 0; i < 4; ++i) {
    old_ids.push_back(append(shadowed_message("user " + std::to_string(i), message_kind::user_prompt)));
    old_ids.push_back(append(shadowed_message(big, message_kind::assistant_output)));
  }
  // current turn's user message (newest -> preserved in tail)
  const size_type user_msg = append(shadowed_message("the current question", message_kind::user_prompt));

  // Pretend this turn's first main request measured 17000 -> over threshold.
  db.record_request_usage(user_msg, 17000, 500);
  std::printf("before compact: exceeds=%d (expect 1)\n", db.exceeds_auto_compact_threshold(sid));

  // ---- compact(): ----
  const size_type summary_id = append(shadowed_message("## summary", message_kind::inline_compact));
  // mark everything except the current user_msg as absorbed
  for (size_type id : old_ids) db.compact_message(id, summary_id);
  // summariser usage pairs with the inline_compact row (pre-compaction prompt)
  db.record_request_usage(summary_id, 40000, 800);

  // what would the same turn's main request now send? (all_not_compacted)
  auto msgs = db.all_not_compacted_messages(sid);
  std::printf("after compact: visible msgs=%zu (expect 2: summary + current user)\n", msgs.size());
  for (auto& m : msgs) std::printf("  id=%u kind=%d\n", m.id_, (int)m.kind_);

  // the main request measured the compacted context (small). Record that.
  db.record_request_usage(user_msg, 900, 200);
  std::printf("after post-compact main request: exceeds=%d (expect 0)\n",
              db.exceeds_auto_compact_threshold(sid));

  // ---- re-check what context_usage actually returns now ----
  auto cu = db.context_usage(sid);
  std::printf("context_usage present=%d value=%d\n", cu.has_value(), (int)cu.value_or(0));
  return 0;
}
