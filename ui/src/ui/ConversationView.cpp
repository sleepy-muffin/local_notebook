#include "ui/ConversationView.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "client.hpp"
#include "settings/settings.hpp"
#include "support/KnownModels.hpp"
#include "support/ReasoningText.hpp"
#include "support/i18n.hpp"
#include "ui/MessageDocument.hpp"
#include "ui/StatusNotice.hpp"
#include "ui/TimeFormat.hpp"
#include "ui/TranscriptCanvas.hpp"
#include "ui/theme.hpp"

namespace ui {

namespace {

QString css_color(const QColor& color) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

QString from_utf8(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::u16string to_u16(const QString& value) {
  return std::u16string(
      reinterpret_cast<const char16_t*>(value.utf16()), static_cast<std::size_t>(value.size()));
}

// Collapses a tool payload (arguments JSON, tool output) onto a single short
// line, so a tool strip stays a one-line note instead of a wall of text.
QString compact_line(const QString& value, int limit) {
  QString flat = value.simplified();

  if (flat.size() > limit) {
    flat = flat.left(limit - 1) + QChar(0x2026);
  }

  return flat;
}

// Streaming Markdown is re-rendered at most this often (ms), so a long reply is
// not re-parsed on every poll tick.
constexpr int kStreamRenderMs = 50;

} // namespace

// -----------------------------------------------------------------------------
// ReasoningSelector
// -----------------------------------------------------------------------------

// The composer's reasoning-effort control. It is deliberately bare: no frame, no
// background, no border — just the level's text with a chevron on its right, so
// it reads as part of the composer surface. Clicking anywhere on it (text or
// chevron) opens the dropdown. The app has no Q_OBJECT, so the widget paints
// itself and dispatches through a plain std::function.
class ReasoningSelector : public QWidget {
public:
  explicit ReasoningSelector(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("ReasoningSelector"));
    setCursor(Qt::PointingHandCursor);
    // Never take focus: the composer field keeps it, so typing is uninterrupted.
    setFocusPolicy(Qt::NoFocus);
    setFont(ui_font());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Fully transparent: no background is ever painted. Hover only brightens the
    // text and chevron, never fills a surface.
    setAttribute(Qt::WA_Hover, true);
    setAutoFillBackground(false);
    setFixedHeight(28);
  }

  // Shows `current` and offers `levels`. The stored value is always included by
  // the caller, so a level the model no longer lists is still shown.
  void set_levels(const std::vector<std::string>& levels, const std::string& current) {
    levels_ = levels;
    current_ = current;
    update_geometry();
    update();
  }

  void set_on_selected(std::function<void(const std::string&)> handler) {
    on_selected_ = std::move(handler);
  }

  // Re-reads the font (a metric change) and repaints after a theme change.
  void refresh_style() {
    setFont(ui_font());
    update_geometry();
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    if (current_.empty()) {
      return;
    }

    const ThemeColors& colors = theme().colors;
    const QColor ink = (hovered_ || menu_open_) ? colors.text : colors.text_muted;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int text_pad = 6;
    const int arrow = 9;
    const QRect text_rect = rect().adjusted(text_pad, 0, -(arrow + text_pad), 0);

    painter.setPen(ink);
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, from_utf8(current_));

    // A small chevron on the right, the only chrome the control carries.
    const qreal cx = static_cast<qreal>(width()) - text_pad - arrow / 2.0;
    const qreal cy = height() / 2.0;
    QPen pen(ink);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPointF(cx - 3.0, cy - 1.5), QPointF(cx, cy + 1.5));
    painter.drawLine(QPointF(cx, cy + 1.5), QPointF(cx + 3.0, cy - 1.5));
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && !levels_.empty()) {
      open_menu();
      return;
    }

    QWidget::mouseReleaseEvent(event);
  }

  void enterEvent(QEnterEvent* event) override {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
  }

private:
  void update_geometry() {
    const QFontMetrics metrics(font());
    int text_width = 0;

    for (const std::string& level: levels_) {
      text_width = std::max(text_width, metrics.horizontalAdvance(from_utf8(level)));
    }

    // Text, a gap, the chevron, and the same padding on both sides.
    setFixedWidth(text_width + 9 + 12);
  }

  void open_menu() {
    QMenu menu(this);

    for (const std::string& level: levels_) {
      QAction* action = menu.addAction(from_utf8(level));
      action->setCheckable(true);
      action->setChecked(level == current_);
      action->setData(from_utf8(level));
    }

    menu_open_ = true;
    update();

    // Drop above the control: it sits at the very bottom of the window.
    QAction* chosen = menu.exec(mapToGlobal(QPoint(0, -menu.sizeHint().height() - 2)));

    menu_open_ = false;
    update();

    if (chosen != nullptr && on_selected_ && chosen->data().toString() != from_utf8(current_)) {
      on_selected_(chosen->data().toString().toStdString());
    }
  }

  std::vector<std::string> levels_;
  std::string current_;
  std::function<void(const std::string&)> on_selected_;
  bool hovered_ = false;
  bool menu_open_ = false;
};

// -----------------------------------------------------------------------------
// SteerQueue
// -----------------------------------------------------------------------------

// The strip above the composer listing prompts typed while a turn was running
// and not yet delivered to it. The queue itself lives in the controller, so the
// worker may empty it at any moment; this only mirrors it. Each row can be
// dismissed before the worker takes it. Painted by hand, like the composer's
// other bare controls, so it follows the theme exactly and elides long prompts
// itself instead of widening the window.
class SteerQueue : public QWidget {
public:
  explicit SteerQueue(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("SteerQueue"));
    setFont(ui_font());
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setVisible(false);
  }

  void set_entries(std::vector<ConversationController::steer_entry> entries) {
    entries_ = std::move(entries);

    if (hover_row_ >= static_cast<int>(entries_.size())) {
      hover_row_ = -1;
      hover_remove_ = false;
    }

    update_geometry();
    update();
    setVisible(!entries_.empty());
  }

  void set_on_remove(std::function<void(std::uint64_t)> handler) {
    on_remove_ = std::move(handler);
  }

  // Re-reads the font (a metric change) and repaints after a theme change.
  void refresh_style() {
    setFont(ui_font());
    update_geometry();
    update();
  }

  [[nodiscard]] QSize sizeHint() const override {
    return QSize(0, height_for(static_cast<int>(entries_.size())));
  }

protected:
  void paintEvent(QPaintEvent*) override {
    if (entries_.empty()) {
      return;
    }

    const ThemeColors& colors = theme().colors;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QFontMetrics fm(font());

    for (int row = 0; row < static_cast<int>(entries_.size()); ++row) {
      const QRect box = row_rect(row);

      painter.setPen(Qt::NoPen);
      painter.setBrush(row == hover_row_ ? row_hover_background() : colors.tool_background);
      painter.drawRoundedRect(box, 4, 4);

      // A chevron marker: this prompt joins the running turn rather than
      // standing as its own message yet.
      QPen marker(colors.tool_text);
      marker.setWidthF(1.3);
      marker.setCapStyle(Qt::RoundCap);
      marker.setJoinStyle(Qt::RoundJoin);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(marker);

      const qreal mx = box.left() + 12.0;
      const qreal my = static_cast<qreal>(box.center().y());
      painter.drawLine(QPointF(mx - 3.0, my - 3.0), QPointF(mx, my));
      painter.drawLine(QPointF(mx, my), QPointF(mx - 3.0, my + 3.0));

      // The prompt, one line, elided.
      const QRect text_rect = box.adjusted(22, 0, -(kRemoveWidth + 6), 0);
      painter.setPen(colors.tool_text);
      painter.drawText(
          text_rect, Qt::AlignLeft | Qt::AlignVCenter,
          fm.elidedText(from_utf8(entries_[static_cast<std::size_t>(row)].text), Qt::ElideRight,
                        text_rect.width()));

      // The dismiss cross, brightening under the pointer.
      const QRect remove = remove_rect(row);
      const bool active = row == hover_row_ && hover_remove_;

      QPen cross(active ? colors.accent : colors.text_muted);
      cross.setWidthF(1.3);
      cross.setCapStyle(Qt::RoundCap);
      painter.setPen(cross);

      const QPointF centre = QRectF(remove).center();
      const qreal radius = 3.5;
      painter.drawLine(QPointF(centre.x() - radius, centre.y() - radius),
                       QPointF(centre.x() + radius, centre.y() + radius));
      painter.drawLine(QPointF(centre.x() + radius, centre.y() - radius),
                       QPointF(centre.x() - radius, centre.y() + radius));
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const int row = row_at(event->pos());
    const bool over_remove = row >= 0 && remove_rect(row).contains(event->pos());

    if (row != hover_row_ || over_remove != hover_remove_) {
      hover_row_ = row;
      hover_remove_ = over_remove;
      setCursor(over_remove ? Qt::PointingHandCursor : Qt::ArrowCursor);
      update();
    }

    QWidget::mouseMoveEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    if (hover_row_ != -1) {
      hover_row_ = -1;
      hover_remove_ = false;
      setCursor(Qt::ArrowCursor);
      update();
    }

    QWidget::leaveEvent(event);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      const int row = row_at(event->pos());

      if (row >= 0 && remove_rect(row).contains(event->pos()) && on_remove_) {
        on_remove_(entries_[static_cast<std::size_t>(row)].serial);
        return;
      }
    }

    QWidget::mousePressEvent(event);
  }

private:
  static constexpr int kRemoveWidth = 18;
  static constexpr int kRowSpacing = 2;

  [[nodiscard]] int row_height() const { return QFontMetrics(font()).height() + 6; }

  [[nodiscard]] int height_for(int rows) const {
    if (rows <= 0) {
      return 0;
    }

    return rows * row_height() + (rows - 1) * kRowSpacing + 4;
  }

  void update_geometry() {
    setFixedHeight(height_for(static_cast<int>(entries_.size())));
    updateGeometry();
  }

  [[nodiscard]] QRect row_rect(int row) const {
    const int pad = metrics().page_margin;

    return QRect(
        pad, row * (row_height() + kRowSpacing) + 2, std::max(0, width() - 2 * pad), row_height());
  }

  [[nodiscard]] QRect remove_rect(int row) const {
    const QRect box = row_rect(row);
    return QRect(box.right() - kRemoveWidth, box.top(), kRemoveWidth, box.height());
  }

  [[nodiscard]] int row_at(const QPoint& pos) const {
    for (int row = 0; row < static_cast<int>(entries_.size()); ++row) {
      if (row_rect(row).contains(pos)) {
        return row;
      }
    }

    return -1;
  }

  std::vector<ConversationController::steer_entry> entries_;
  std::function<void(std::uint64_t)> on_remove_;
  int hover_row_ = -1;
  bool hover_remove_ = false;
};

ConversationView::ConversationView(
    felis::bell::client& api, const lnb::Settings& settings, std::uint32_t session_id,
    felis::async::multi_executor<void()>& executor, QWidget* parent)
    : QWidget(parent), api_(api), settings_(settings), session_id_(session_id),
      controller_(api, executor, session_id) {
  const lnb::ThemeMetrics& m = metrics();

  setObjectName(QStringLiteral("ConversationView"));
  setAttribute(Qt::WA_StyledBackground, true);
  setMinimumWidth(0);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  transcript_ = new TranscriptCanvas(this);
  transcript_->set_on_reach_top([this]() { load_older(); });

  transcript_->set_on_copy([this](std::uint32_t message_id) {
    for (const felis::bell::shadowed_message& message: messages_) {
      if (message.id_ == message_id) {
        // A reasoning row's visible text is derived from its payload, not stored
        // in `content_`, so copy the same text the strip shows.
        if (message.kind_ == felis::bell::message_kind::assistant_reasoning_payload) {
          QApplication::clipboard()->setText(
              from_utf8(reasoning::display_text(message.reasoning_payload_.value_or(std::string{}))));
        } else {
          QApplication::clipboard()->setText(from_utf8(message.content_));
        }
        return;
      }
    }
  });

  transcript_->set_on_edit([this](std::uint32_t message_id) {
    for (const felis::bell::shadowed_message& message: messages_) {
      if (message.id_ == message_id) {
        prompt_edit(message_id, from_utf8(message.content_));
        return;
      }
    }
  });

  transcript_->set_on_delete([this](std::uint32_t message_id) {
    QMessageBox dialog(
        QMessageBox::Warning, trs("Delete Message"),
        trs("Are you sure you want to delete this message?"), QMessageBox::NoButton,
        this);
    dialog.setInformativeText(trs(
        "Delete This Message removes only it. Delete This and Following removes it "
        "and every message after it. This cannot be undone."));

    QAbstractButton* only =
        dialog.addButton(trs("Delete This Message"), QMessageBox::DestructiveRole);
    QAbstractButton* following =
        dialog.addButton(trs("Delete This and Following"), QMessageBox::DestructiveRole);
    QPushButton* cancel = dialog.addButton(QMessageBox::Cancel);
    dialog.setDefaultButton(cancel);
    dialog.exec();

    if (dialog.clickedButton() == only) {
      controller_.delete_message_only(message_id);
    } else if (dialog.clickedButton() == following) {
      controller_.delete_message(message_id);
    }
  });

  // Jump from an assistant reply back to the prompt it answers: walk the loaded
  // history to the nearest preceding user turn, widen the window if that turn is
  // older than what is on screen, then scroll it to the top.
  transcript_->set_on_previous_prompt([this](std::uint32_t message_id) {
    std::size_t index = messages_.size();
    for (std::size_t i = 0; i < messages_.size(); ++i) {
      if (messages_[i].id_ == message_id) {
        index = i;
        break;
      }
    }

    while (index > 0) {
      --index;
      if (messages_[index].kind_ != felis::bell::message_kind::user_prompt) {
        continue;
      }

      if (index < window_start_) {
        const TranscriptCanvas::ScrollAnchor anchor = transcript_->anchor();
        window_start_ = index;
        rebuild_transcript(anchor, false);
      }

      transcript_->scroll_to_message(messages_[index].id_);
      return;
    }
  });

  layout->addWidget(transcript_, 1);

  auto* composer = new QWidget(this);
  composer->setObjectName(QStringLiteral("ComposerBar"));
  composer->setAttribute(Qt::WA_StyledBackground, true);
  composer->setAutoFillBackground(false);
  composer_bar_ = composer;

  auto* composer_layout = new QHBoxLayout(composer);
  composer_layout->setContentsMargins(
      m.page_margin, m.page_margin / 2, m.page_margin, m.page_margin);
  composer_layout->setSpacing(8);

  input_ = new QPlainTextEdit(composer);
  input_->setObjectName(QStringLiteral("Composer"));
  input_->setPlaceholderText(
      settings_.enter_with_modifier
          ? trs("Type a message...  (Ctrl+Enter to send, Enter for a new line)")
          : trs("Type a message...  (Enter to send, Shift+Enter for a new line)"));
  input_->setTabChangesFocus(true);
  input_->setFont(chat_font());
  input_->installEventFilter(this);

  auto* buttons = new QVBoxLayout();
  buttons->setSpacing(4);

  expand_button_ = new QToolButton(composer);
  expand_button_->setObjectName(QStringLiteral("ComposerExpand"));
  expand_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  expand_button_->setIconSize(QSize(16, 16));
  expand_button_->setFixedSize(28, 28);
  expand_button_->setAutoRaise(true);
  expand_button_->setFocusPolicy(Qt::NoFocus);
  expand_button_->setCursor(Qt::PointingHandCursor);

  send_button_ = new QToolButton(composer);
  send_button_->setObjectName(QStringLiteral("SendButton"));
  send_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  send_button_->setIconSize(QSize(16, 16));
  send_button_->setFixedSize(28, 28);
  send_button_->setAutoRaise(true);
  send_button_->setFocusPolicy(Qt::NoFocus);
  send_button_->setCursor(Qt::PointingHandCursor);

  cancel_button_ = new QToolButton(composer);
  cancel_button_->setObjectName(QStringLiteral("CancelButton"));
  cancel_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  cancel_button_->setIconSize(QSize(16, 16));
  cancel_button_->setFixedSize(28, 28);
  cancel_button_->setAutoRaise(true);
  cancel_button_->setFocusPolicy(Qt::NoFocus);
  cancel_button_->setCursor(Qt::PointingHandCursor);
  cancel_button_->setVisible(false);

  buttons->addWidget(expand_button_, 0, Qt::AlignTop | Qt::AlignRight);
  buttons->addStretch(1);

  auto* bottom_actions = new QHBoxLayout();
  bottom_actions->setContentsMargins(0, 0, 0, 0);
  bottom_actions->setSpacing(2);
  bottom_actions->addStretch(1);

  // The reasoning control sits immediately to the left of the send button, at the
  // block's bottom-right. Reading it as part of the composer, it is borderless and
  // fully transparent; the group it belongs to takes width from the input field.
  reasoning_selector_ = new ReasoningSelector(composer);
  reasoning_selector_->set_on_selected([this](const std::string& level) {
    api_.set_session_reasoning_effort(felis::bell::size_type(session_id_), level);
    refresh_reasoning_selector();
  });

  bottom_actions->addWidget(reasoning_selector_, 0, Qt::AlignVCenter);
  bottom_actions->addWidget(send_button_, 0, Qt::AlignVCenter);
  bottom_actions->addWidget(cancel_button_, 0, Qt::AlignVCenter);

  buttons->addLayout(bottom_actions);

  composer_layout->addWidget(input_, 1);
  composer_layout->addLayout(buttons);

  notice_ = new StatusNotice(this);
  layout->addWidget(notice_);

  // Prompts typed while a turn runs wait here until the worker takes them; each
  // row can be dismissed before delivery. It sits directly above the composer.
  steer_queue_ = new SteerQueue(this);
  steer_queue_->set_on_remove([this](std::uint64_t serial) {
    if (controller_.remove_steer(serial)) {
      refresh_steer_queue();
    }
  });
  layout->addWidget(steer_queue_);

  layout->addWidget(composer);

  apply_composer_style();
  apply_composer_height();

  refresh_reasoning_selector();

  stream_timer_ = new QTimer(this);
  stream_timer_->setSingleShot(true);
  stream_timer_->setInterval(kStreamRenderMs);
  connect(stream_timer_, &QTimer::timeout, this, [this]() { flush_streaming_text(); });

  connect(expand_button_, &QToolButton::clicked, this, [this]() { toggle_composer_height(); });
  connect(send_button_, &QToolButton::clicked, this, [this]() { submit(); });
  connect(cancel_button_, &QToolButton::clicked, this, [this]() { controller_.cancel(); });

  controller_.set_on_changed([this]() {
    reload();
    if (on_changed_) {
      on_changed_();
    }
  });

  controller_.set_on_stream([this](const std::string& text) {
    if (!text_streaming_) {
      // Only the last canvas message can be updated in place, so the reasoning
      // bubble must be materialised before the reply becomes the last one.
      if (reasoning_streaming_ && has_pending_reasoning_) {
        transcript_->set_streaming_blocks(
            make_strip_blocks(trs("Reasoning"), pending_reasoning_text_));
        has_pending_reasoning_ = false;
      }

      text_streaming_ = true;
      transcript_->begin_streaming(render::Role::Assistant);
    }

    // `on_text` delivers incremental deltas (one per streamed token/chunk), so
    // they must be accumulated here. Assigning would keep only the newest
    // fragment, which is why a reply used to show just the last token (often a
    // stray `.`) until the turn finished and the DB round-trip rebuilt it.
    pending_stream_text_ += from_utf8(text);
    has_pending_stream_ = true;

    if (!stream_timer_->isActive()) {
      stream_timer_->start();
    }
  });

  controller_.set_on_reasoning([this](const std::string& text) {
    // Reasoning streams live as its own subtle bubble, ahead of the reply.
    if (text_streaming_) {
      // The reply owns the growing tail now, so a late reasoning delta cannot be
      // drawn in place; the stored row shows it after the next reload.
      return;
    }

    if (!reasoning_streaming_) {
      reasoning_streaming_ = true;
      transcript_->begin_streaming(render::Role::Tool);
    }

    pending_reasoning_text_ += from_utf8(text);
    has_pending_reasoning_ = true;

    if (!stream_timer_->isActive()) {
      stream_timer_->start();
    }
  });

  controller_.set_on_status([this](const std::string& status) {
    streaming_status_ = from_utf8(status);

    // The status note is only a placeholder for a reply that has produced no
    // text or reasoning yet; anything already streaming owns the bubble.
    if (text_streaming_ || reasoning_streaming_) {
      return;
    }

    text_streaming_ = true;
    transcript_->begin_streaming(render::Role::Assistant);
    has_pending_stream_ = true;

    if (!stream_timer_->isActive()) {
      stream_timer_->start();
    }
  });

  controller_.set_on_error([this](const std::string& reason) {
    if (notice_ != nullptr) {
      notice_->set_content(QStringLiteral("warning"), from_utf8(reason));
    }
  });

  controller_.set_on_steered([this](std::uint32_t message_id) {
    // The worker just stored the steered prompt; mark its row so the reload that
    // follows (from `on_changed`) can label it.
    steered_ids_.insert(message_id);
  });

  reload();
}

void ConversationView::reload() {
  const TranscriptCanvas::ScrollAnchor anchor = transcript_->anchor();
  // Use the reader's intent, not the current geometry: this rebuild drops the
  // streaming bubble, so at_bottom() could report "at the bottom" for someone
  // who had scrolled up.
  const bool follow = transcript_->sticking();

  if (stream_timer_ != nullptr) {
    stream_timer_->stop();
  }

  has_pending_reasoning_ = false;
  has_pending_stream_ = false;
  pending_reasoning_text_.clear();
  pending_stream_text_.clear();
  streaming_status_.clear();
  reasoning_streaming_ = false;
  text_streaming_ = false;

  messages_ = api_.messages(felis::bell::size_type(session_id_));

  const std::size_t limit =
      static_cast<std::size_t>(std::max(1, settings_.rendered_message_limit));
  window_start_ = messages_.size() > limit ? messages_.size() - limit : 0;

  rebuild_transcript(anchor, follow);

  set_busy(controller_.busy());
  refresh_reasoning_selector();
}

render::Document ConversationView::build_document() const {
  render::Document document;

  for (std::size_t i = window_start_; i < messages_.size(); ++i) {
    append_rendered_message(document, messages_[i]);
  }

  return document;
}

void ConversationView::append_rendered_message(
    render::Document& document, const felis::bell::shadowed_message& message) const {
  using felis::bell::message_kind;

  // The readable turns are full messages; tool calls, their results and inline
  // summaries are folded into a compact strip so the plumbing is visible without
  // crowding the transcript, and hidden reasoning is never shown at all. A strip
  // carries a short bold title ("Tool call · name" / "Tool result · name" /
  // "Inline summary") drawn at content size, so its parts read as clearly
  // different things.
  render::Role role = render::Role::Assistant;
  bool markdown = true;
  bool editable = message.id_ != 0;
  QString title;
  QString text = from_utf8(message.content_);

  switch (message.kind_) {
    case message_kind::user_prompt:
      role = render::Role::User;
      markdown = false;
      break;
    case message_kind::assistant_output:
      break;
    case message_kind::assistant_reasoning_payload: {
      // Reasoning is context work, not a turn: it reads as a subtle strip like a
      // tool note. The payload is display-only, so its text is derived here; an
      // encrypted-only item falls back to its raw JSON.
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      title = trs("Reasoning");
      text = from_utf8(
          reasoning::display_text(message.reasoning_payload_.value_or(std::string{})));

      // An old-schema row may carry an empty payload, and a provider can send a
      // whitespace-only summary; never draw an empty strip either way.
      if (text.trimmed().isEmpty()) {
        return;
      }
      break;
    }
    case message_kind::inline_compact:
      // A summary is generated text, not a turn: it gets the same subtle
      // colours and smaller size as a tool strip.
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      title = trs("Inline summary");
      break;
    case message_kind::assistant_tool_call: {
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      const QString name =
          message.tool_call_.has_value() ? from_utf8(message.tool_call_->tool_name_) : QString();
      title = name.isEmpty() ? trs("Tool call") : trs("Tool call · %1").arg(name);
      text = compact_line(text, 160);
      break;
    }
    case message_kind::tool_result: {
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      const QString name =
          message.tool_call_.has_value() ? from_utf8(message.tool_call_->tool_name_) : QString();
      title = name.isEmpty() ? trs("Tool result") : trs("Tool result · %1").arg(name);
      const QString output = compact_line(text, 200);
      text = output.isEmpty() ? trs("(no output)") : output;
      break;
    }
    default:
      return;
  }

  // A row absorbed by an inline summary carries that summary's id, shown beside
  // the message id in the hover strip so the folded-away rows still say where
  // they went. A prompt steered into a running turn says so instead, since it
  // was answered without the user ever pressing send for a new turn.
  std::u16string status;
  if (message.compacted_at_ != 0) {
    status = to_u16(trs("Compacted into #%1").arg(message.compacted_at_));
  } else if (steered_ids_.find(message.id_) != steered_ids_.end()) {
    status = to_u16(trs("Steered"));
  }

  render::MessageMeta meta = make_message_meta(
      role, message.id_, to_u16(format_message_time(message.made_at_)), std::u16string{}, status,
      editable, to_u16(text));

  std::vector<render::Block> blocks;

  if (!title.isEmpty()) {
    // A strip: the title line plus the detail line in one block, so the two
    // share the strip's single (smaller) tool font size. The title is bold only,
    // never larger, so it reads as a label rather than a heading.
    blocks = make_strip_blocks(title, text);
  } else {
    blocks = markdown ? make_markdown_blocks(text.toStdString())
                      : make_plain_blocks(text.toStdString());
  }

  append_message(document, std::move(meta), std::move(blocks));
}

std::vector<render::Block>
ConversationView::make_strip_blocks(const QString& title, const QString& body) const {
  render::Block strip;
  strip.kind = render::BlockKind::Paragraph;
  strip.text = to_u16(title);
  render::Span span;
  span.length = static_cast<std::uint32_t>(strip.text.size());
  span.bold = true;
  strip.spans.push_back(span);
  strip.text += u'\n';
  strip.text += to_u16(body);
  return {std::move(strip)};
}

void ConversationView::rebuild_transcript(
    const TranscriptCanvas::ScrollAnchor& anchor, bool follow_bottom) {
  transcript_->set_document(build_document(), anchor, follow_bottom);
}

void ConversationView::load_older() {
  if (window_start_ == 0) {
    return;
  }

  const TranscriptCanvas::ScrollAnchor anchor = transcript_->anchor();
  const std::size_t page = static_cast<std::size_t>(std::max(1, settings_.history_page_size));
  window_start_ = window_start_ > page ? window_start_ - page : 0;
  rebuild_transcript(anchor, false);
}

void ConversationView::refresh_steer_queue() {
  if (steer_queue_ != nullptr) {
    steer_queue_->set_entries(controller_.queued_steers());
  }
}

void ConversationView::refresh_style() {
  apply_composer_style();
  apply_composer_height();

  if (transcript_ != nullptr) {
    transcript_->refresh_style();
  }

  if (steer_queue_ != nullptr) {
    steer_queue_->refresh_style();
  }

  refresh_reasoning_selector();
}

void ConversationView::refresh_reasoning_selector() {
  if (reasoning_selector_ == nullptr) {
    return;
  }

  // The stored value may name a level the model no longer lists; the model's
  // ladder is what the composer offers, and `set_levels` shows the stored value
  // even if it falls outside that set. An unset level starts on the model's
  // default, the same one the assistant form offers.
  const std::optional<felis::bell::session> stored =
      api_.load_session(felis::bell::size_type(session_id_));
  std::string current = stored.has_value() ? stored->reasoning_effort() : std::string{};

  const std::string model = api_.current_assistant().model_;
  if (current.empty()) {
    current = known_models::default_reasoning(model);
  }

  std::vector<std::string> levels = known_models::reasoning_levels(model);
  if (std::find(levels.begin(), levels.end(), current) == levels.end()) {
    levels.push_back(current);
  }

  reasoning_selector_->set_levels(levels, current);
}

void ConversationView::flush_streaming_text() {
  // Reasoning owns the tail only until the reply starts; once the reply message
  // is the last one, this bubble can no longer be rewritten in place.
  if (has_pending_reasoning_ && reasoning_streaming_ && !text_streaming_) {
    transcript_->set_streaming_blocks(
        make_strip_blocks(trs("Reasoning"), pending_reasoning_text_));
  }
  has_pending_reasoning_ = false;

  if (!has_pending_stream_ || !text_streaming_) {
    return;
  }

  has_pending_stream_ = false;
  // Prefer the accumulated reply; before any text arrives, show the status note
  // ("Compacting context…", "Running …") so the bubble is not blank.
  const QString& rendered =
      pending_stream_text_.isEmpty() ? streaming_status_ : pending_stream_text_;
  transcript_->set_streaming_blocks(make_markdown_blocks(rendered.toStdString()));
}

void ConversationView::poll() {
  controller_.poll();
  set_busy(controller_.busy());
  // The worker drains the queue as soon as it takes a steering prompt, so the
  // strip must follow it on every tick, not only when the user edits it.
  refresh_steer_queue();
}

void ConversationView::set_failure_notice_handler(std::function<void()> handler) {
  if (notice_ != nullptr) {
    notice_->set_on_click(std::move(handler));
  }
}

void ConversationView::set_busy(bool busy) {
  // The send button stays live while a turn runs: pressing it steers that turn
  // instead of starting a new one.
  if (busy_ == busy) {
    return;
  }

  busy_ = busy;
  cancel_button_->setVisible(busy);

  if (send_button_ != nullptr) {
    send_button_->setToolTip(busy_ ? trs("Steer the running turn") : trs("Send"));
  }

  if (busy && notice_ != nullptr) {
    notice_->setVisible(false);
  }
}

void ConversationView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);

  if (style_stale_) {
    style_stale_ = false;
    refresh_style();
  }
}

bool ConversationView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == input_ && event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);

    const bool is_enter = key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter;

    if (settings_.enter_with_modifier) {
      if (is_enter && (key_event->modifiers() & Qt::ControlModifier)) {
        submit();
        return true;
      }
    } else {
      if (is_enter && !(key_event->modifiers() & Qt::ShiftModifier)) {
        submit();
        return true;
      }
    }
  }

  return QWidget::eventFilter(watched, event);
}

void ConversationView::prompt_edit(std::uint32_t message_id, const QString& current) {
  bool accepted = false;

  const QString edited = QInputDialog::getMultiLineText(
      this, trs("Edit message"), trs("Message:"), current, &accepted);

  if (accepted && edited != current) {
    api_.edit_message(felis::bell::size_type(message_id), edited.toStdString());
    reload();
    if (on_changed_) {
      on_changed_();
    }
  }
}

void ConversationView::apply_composer_style() {
  const ThemeColors& colors = theme().colors;

  setStyleSheet(QStringLiteral("QWidget#ConversationView { background: %1; }")
                    .arg(css_color(colors.conversation_background)));

  input_->setStyleSheet(QStringLiteral("QPlainTextEdit#Composer { background: %1; border: none;"
                                       " padding: 4px 2px; color: %2; }")
                            .arg(css_color(colors.input_background), css_color(colors.text)));

  if (composer_bar_ != nullptr) {
    composer_bar_->setStyleSheet(
        QStringLiteral("QWidget#ComposerBar { background: transparent;"
                       " border-top: 1px solid %1; }"
                       "QToolButton#SendButton, QToolButton#ComposerExpand,"
                       "QToolButton#CancelButton {"
                       "  background: transparent; border: none; border-radius: 4px; padding: 0; }"
                       "QToolButton#SendButton:hover, QToolButton#ComposerExpand:hover,"
                       "QToolButton#CancelButton:hover {"
                       "  background: %2; }")
            .arg(css_color(colors.border), css_color(row_hover_background())));
  }

  apply_composer_icons();
}

void ConversationView::apply_composer_icons() {
  const ThemeColors& colors = theme().colors;

  if (send_button_ != nullptr) {
    QIcon icon = resource_icon(QStringLiteral("send"), colors.accent, colors.text_muted);

    if (icon.isNull()) {
      icon = style()->standardIcon(QStyle::SP_ArrowUp);
    }

    send_button_->setIcon(icon);
    send_button_->setToolTip(busy_ ? trs("Steer the running turn") : trs("Send"));
  }

  if (expand_button_ != nullptr) {
    const bool expanded = composer_expanded_;
    QIcon icon = resource_icon(
        expanded ? QStringLiteral("collapse") : QStringLiteral("expand"), colors.text_muted,
        colors.text_muted);

    if (icon.isNull()) {
      icon = style()->standardIcon(expanded ? QStyle::SP_ArrowDown : QStyle::SP_ArrowUp);
    }

    expand_button_->setIcon(icon);
    expand_button_->setToolTip(expanded ? trs("Shrink the composer") : trs("Expand the composer"));
  }

  if (cancel_button_ != nullptr) {
    QIcon icon = resource_icon(QStringLiteral("stop"), colors.text_muted, colors.text_muted);

    if (icon.isNull()) {
      icon = style()->standardIcon(QStyle::SP_MediaStop);
    }

    cancel_button_->setIcon(icon);
    cancel_button_->setToolTip(trs("Cancel"));
  }
}

void ConversationView::apply_composer_height() {
  if (input_ != nullptr) {
    const lnb::ThemeMetrics& m = metrics();
    input_->setFixedHeight(composer_expanded_ ? m.composer_height_expanded : m.composer_height);
  }

  apply_composer_icons();
}

void ConversationView::toggle_composer_height() {
  composer_expanded_ = !composer_expanded_;
  apply_composer_height();
}

void ConversationView::submit() {
  const QString text = input_->toPlainText().trimmed();

  if (text.isEmpty()) {
    return;
  }

  transcript_->scroll_to_bottom();

  if (controller_.busy()) {
    steer(text);
    return;
  }

  if (controller_.send(text.toStdString())) {
    input_->clear();
  }

  set_busy(controller_.busy());
  refresh_steer_queue();
}

void ConversationView::steer(const QString& text) {
  if (!controller_.steer(text.toStdString())) {
    // The turn ended while the user was typing: treat it as a fresh send so the
    // text is never silently swallowed.
    if (controller_.send(text.toStdString())) {
      input_->clear();
    }

    set_busy(controller_.busy());
    return;
  }

  // The row lands later, from the worker (`on_steered`), so nothing is reloaded
  // here: the running reply finishes first, then the queued prompt joins the
  // turn as its next request.
  input_->clear();
  set_busy(controller_.busy());
  refresh_steer_queue();
}

} // namespace ui
