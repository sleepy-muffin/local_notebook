// Standalone runtime check for the append path + the auto-compact predicate.
// Drives db.insert_message the way client::append_message stamps it. Not part of
// the library build.
#include <cstdint>
#include <cstdio>
#include <string>

#include "database.hpp"
#include "message.hpp"

using namespace felis::bell;

int main() {
  const std::string path = "/tmp/auto_compact_check.db";
  std::remove(path.c_str());

  database db(path);

  const std::uint32_t sid = db.insert_session(shadowed_session(0, "test"));
  const size_type session_id(sid);

  // Mirror the client's append path: stamp the owning session, then insert.
  auto append = [&](shadowed_message msg) -> size_type {
    msg.session_id_ = session_id.get();
    return size_type(db.insert_message(msg));
  };

  // Exercises the fixed append path: the message is keyed to its session.
  const size_type user_msg =
    append(shadowed_message("hello", message_kind::user_prompt));
  std::printf("append ok: message id = %u\n", user_msg.get());

  // Below the default threshold (16000).
  db.record_request_usage(user_msg, 1000, 200);
  std::printf("below: usage=%d exceeds=%d (expect usage=1200 exceeds=0)\n",
              static_cast<int>(db.context_usage(session_id).value_or(0)),
              db.exceeds_auto_compact_threshold(session_id));

  // A second request in the same turn pushes total over the threshold.
  db.record_request_usage(user_msg, 15000, 1500);
  std::printf("above: usage=%d exceeds=%d (expect 16500 exceeds=1)\n",
              static_cast<int>(db.context_usage(session_id).value_or(0)),
              db.exceeds_auto_compact_threshold(session_id));

  // A summariser's own request pairs with an inline_compact message: the
  // pre-summary count must not be read as the current context size.
  const size_type summary_msg =
    append(shadowed_message("summary", message_kind::inline_compact));
  db.record_request_usage(summary_msg, 50000, 300);
  std::printf("after summary: has_usage=%d exceeds=%d (expect has_usage=0 exceeds=0)\n",
              db.context_usage(session_id).has_value() ? 1 : 0,
              db.exceeds_auto_compact_threshold(session_id));

  return 0;
}
