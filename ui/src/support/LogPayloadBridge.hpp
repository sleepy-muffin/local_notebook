#pragma once

// A permanent `felis::log` consumer for dumped LLM request bodies.
//
// The backend writes a huge serialised request behind
// `felis::bell::IDENTIFIER_LLM_PAYLOAD` in debug builds. Left alone it lands in
// the assistant's log file, which is both useless and enormous. This bridge
// recognises those records, renders them into readable text and terminates them,
// so nothing reaches the file. Rendered payloads are queued for the inspector,
// which drains them on the UI thread; when the inspector is closed the queue is
// simply capped.

#include <QString>

#include <mutex>
#include <vector>

namespace ui::log_payload_bridge {

struct Payload {
  QString time;
  QString title;
  QString body;
};

// Installs the terminating callback once, for the lifetime of the process.
void install();

// Moves every payload captured since the previous call out of the queue.
std::vector<Payload> take_pending();

} // namespace ui::log_payload_bridge
