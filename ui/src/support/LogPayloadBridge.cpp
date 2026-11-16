#include "support/LogPayloadBridge.hpp"

#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include <felis/log.hpp>

#include "support/LlmPayloadFormat.hpp"
#include "support/LogFormat.hpp"

namespace ui::log_payload_bridge {

namespace log_payload_bridge_detail {

// The inspector history is bounded, and so is this queue: payloads captured
// while the window is closed only need to survive until it opens.
constexpr std::size_t kMaxPending = 256;

struct Queue {
  std::mutex mutex;
  std::deque<Payload> pending;
};

Queue& queue() {
  static Queue instance;
  return instance;
}

void enqueue(const std::string& content) {
  Payload payload;
  payload.time = log_format::current_timestamp();
  payload.title = llm_payload::title(content);
  payload.body = llm_payload::render(content);

  Queue& target = queue();
  std::lock_guard lock(target.mutex);
  target.pending.push_back(std::move(payload));

  while (target.pending.size() > kMaxPending) {
    target.pending.pop_front();
  }
}

} // namespace log_payload_bridge_detail

using namespace log_payload_bridge_detail;

void install() {
  static std::once_flag installed;
  std::call_once(installed, [] {
    felis::log::register_callback(
        [](felis::log::message_type, std::string& content)
            -> felis::log::logging_callback_result {
          if (!llm_payload::is_dump(content)) {
            return felis::log::logging_callback_result::PASS;
          }

          enqueue(content);

          // The record has been captured; keep the raw body out of the file.
          return felis::log::logging_callback_result::TERMINATED;
        }
    );
  });
}

std::vector<Payload> take_pending() {
  Queue& source = queue();
  std::deque<Payload> pending;

  {
    std::lock_guard lock(source.mutex);
    pending.swap(source.pending);
  }

  return std::vector<Payload>(std::make_move_iterator(pending.begin()),
                              std::make_move_iterator(pending.end()));
}

} // namespace ui::log_payload_bridge
