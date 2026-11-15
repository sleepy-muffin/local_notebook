#pragma once

#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <felis/async.hpp>

#include "render/document.hpp"
#include "ui/ConversationController.hpp"
#include "ui/TranscriptCanvas.hpp"

class QEvent;
class QPlainTextEdit;
class QShowEvent;
class QTimer;
class QToolButton;

namespace lnb {
struct Settings;
}

namespace felis::bell {
class client;
}

namespace ui {

class StatusNotice;
class TranscriptCanvas;
class ReasoningSelector;

// The chat pane for one session: transcript on top, composer at the bottom. The
// view is passive — it renders the stored transcript and forwards intent to its
// ConversationController, which drives the turn on the application's shared
// executor. Switching sessions therefore never interrupts a generation.
//
// The transcript itself is a single custom-painted canvas (TranscriptCanvas):
// messages are model data, not widgets, so a long conversation has no widget
// forest to build and selection can span every message and code block.
class ConversationView : public QWidget {
public:
  ConversationView(
      felis::bell::client& api, const lnb::Settings& settings, std::uint32_t session_id,
      felis::async::multi_executor<void()>& executor, QWidget* parent = nullptr
  );

  [[nodiscard]] std::uint32_t session_id() const { return session_id_; }
  [[nodiscard]] bool busy() const { return controller_.busy(); }

  // Requests the session's in-flight turn to stop.
  void cancel() { controller_.cancel(); }

  // Rebuilds the transcript from the database.
  void reload();

  // Re-applies theme colours to the composer and the transcript.
  void refresh_style();

  // Marks the view as needing a restyle. A visible view is restyled at once, so
  // a theme or metric change lands immediately; a hidden one waits for its next
  // show, where the work is cheap and the layout is current.
  void mark_style_stale() {
    if (isVisible()) {
      refresh_style();
      style_stale_ = false;
    } else {
      style_stale_ = true;
    }
  }

  // Called by the window's poll timer; advances the in-flight generation.
  void poll();

  // Wires the failure notice's click action.
  void set_failure_notice_handler(std::function<void()> handler);

  // Called after every reload, so the window can refresh the session list, the
  // title bar and the context readout.
  void set_on_changed(std::function<void()> handler) { on_changed_ = std::move(handler); }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void rebuild_transcript(const TranscriptCanvas::ScrollAnchor& anchor, bool follow_bottom);
  [[nodiscard]] render::Document build_document() const;
  void append_rendered_message(
      render::Document& document, const felis::bell::shadowed_message& message) const;

  void load_older();
  void prompt_edit(std::uint32_t message_id, const QString& current);
  void flush_streaming_text();
  void submit();
  // Injects a prompt into the running turn without tearing down the reply that
  // is already on screen.
  void steer(const QString& text);
  void set_busy(bool busy);

  void apply_composer_style();
  void apply_composer_height();
  void toggle_composer_height();
  void apply_composer_icons();
  void refresh_reasoning_selector();

  felis::bell::client& api_;
  const lnb::Settings& settings_;
  std::uint32_t session_id_;
  ConversationController controller_;

  TranscriptCanvas* transcript_ = nullptr;

  // Full history for the session, plus the first message index of the window
  // currently handed to the canvas.
  std::vector<felis::bell::shadowed_message> messages_;
  std::size_t window_start_ = 0;

  bool busy_ = false;
  bool style_stale_ = false;

  // Prompts injected into a running turn, so their rows can say so.
  std::set<std::uint32_t> steered_ids_;

  QPlainTextEdit* input_ = nullptr;
  QToolButton* send_button_ = nullptr;
  QToolButton* expand_button_ = nullptr;
  QToolButton* cancel_button_ = nullptr;
  ReasoningSelector* reasoning_selector_ = nullptr;

  QWidget* composer_bar_ = nullptr;
  StatusNotice* notice_ = nullptr;

  bool composer_expanded_ = false;

  // Streaming state.
  bool streaming_started_ = false;
  QString streaming_status_;
  QTimer* stream_timer_ = nullptr;
  QString pending_stream_text_;
  bool has_pending_stream_ = false;

  std::function<void()> on_changed_;
};

} // namespace ui
