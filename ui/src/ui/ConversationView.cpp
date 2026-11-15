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
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "client.hpp"
#include "settings/settings.hpp"
#include "support/i18n.hpp"
#include "ui/MessageDocument.hpp"
#include "ui/StatusNotice.hpp"
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
        QApplication::clipboard()->setText(from_utf8(message.content_));
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
    if (!streaming_started_) {
      streaming_started_ = true;
      transcript_->begin_streaming();
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

  controller_.set_on_reasoning([this](const std::string&) {
    // Reasoning is not shown in the transcript; it is context work only.
  });

  controller_.set_on_status([this](const std::string& status) {
    streaming_status_ = from_utf8(status);

    if (!streaming_started_) {
      streaming_started_ = true;
      transcript_->begin_streaming();
    }

    // Status is only a placeholder shown until the reply text starts arriving;
    // it must not be mixed into the accumulated reply (see the flush below,
    // which prefers reply text and falls back to this status).
    if (pending_stream_text_.isEmpty()) {
      has_pending_stream_ = true;

      if (!stream_timer_->isActive()) {
        stream_timer_->start();
      }
    }
  });

  controller_.set_on_error([this](const std::string& reason) {
    if (notice_ != nullptr) {
      notice_->set_content(QStringLiteral("warning"), from_utf8(reason));
    }
  });

  reload();
}

void ConversationView::reload() {
  const TranscriptCanvas::ScrollAnchor anchor = transcript_->anchor();
  const bool follow = transcript_->at_bottom();

  if (stream_timer_ != nullptr) {
    stream_timer_->stop();
  }

  has_pending_stream_ = false;
  pending_stream_text_.clear();
  streaming_status_.clear();
  streaming_started_ = false;

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

  // The readable turns are full messages; tool calls and their results are
  // folded into a compact strip so the plumbing is visible without crowding the
  // transcript, and hidden reasoning is never shown at all. A strip carries a
  // short bold title ("Tool call · name" / "Tool result · name") drawn at
  // content size, so a call and its result read as clearly different things.
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
    case message_kind::inline_compact:
      markdown = false;
      break;
    case message_kind::assistant_tool_call: {
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      const QString name =
          message.tool_call_.has_value() ? from_utf8(message.tool_call_->tool_name_) : QString();
      title = name.isEmpty() ? trs("Tool call")
                             : QStringLiteral("Tool call \u00b7 %1").arg(name);
      text = compact_line(text, 160);
      break;
    }
    case message_kind::tool_result: {
      role = render::Role::Tool;
      markdown = false;
      editable = false;
      const QString name =
          message.tool_call_.has_value() ? from_utf8(message.tool_call_->tool_name_) : QString();
      title = name.isEmpty() ? trs("Tool result")
                             : QStringLiteral("Tool result \u00b7 %1").arg(name);
      const QString output = compact_line(text, 200);
      text = output.isEmpty() ? trs("(no output)") : output;
      break;
    }
    default:
      return;
  }

  render::MessageMeta meta = make_message_meta(
      role, message.id_, std::u16string{}, std::u16string{}, editable, to_u16(text));

  std::vector<render::Block> blocks;

  if (!title.isEmpty()) {
    // The strip's title row: bold, at normal content size, tight above the
    // detail line that follows it.
    render::Block heading;
    heading.kind = render::BlockKind::Paragraph;
    heading.full_scale = true;
    heading.text = to_u16(title);
    render::Span span;
    span.length = static_cast<std::uint32_t>(heading.text.size());
    span.bold = true;
    heading.spans.push_back(span);
    blocks.push_back(std::move(heading));
  }

  auto body = markdown ? make_markdown_blocks(text.toStdString())
                       : make_plain_blocks(text.toStdString());
  blocks.insert(blocks.end(), std::make_move_iterator(body.begin()),
                std::make_move_iterator(body.end()));

  append_message(document, std::move(meta), std::move(blocks));
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

void ConversationView::refresh_style() {
  apply_composer_style();
  apply_composer_height();

  if (transcript_ != nullptr) {
    transcript_->refresh_style();
  }

  refresh_reasoning_selector();
}

void ConversationView::refresh_reasoning_selector() {
  if (reasoning_selector_ == nullptr) {
    return;
  }

  // The stored value may name a level the model no longer lists; the fixed ladder
  // below is what the composer offers, and `set_levels` shows the stored value
  // even if it falls outside that set.
  const std::optional<felis::bell::session> stored =
      api_.load_session(felis::bell::size_type(session_id_));
  std::string current = stored.has_value() ? stored->reasoning_effort() : std::string{};
  if (current.empty()) {
    current = "none";
  }

  static const std::vector<std::string> kLevels{"none", "low", "medium", "high"};
  std::vector<std::string> levels = kLevels;
  if (std::find(levels.begin(), levels.end(), current) == levels.end()) {
    levels.push_back(current);
  }

  reasoning_selector_->set_levels(levels, current);
}

void ConversationView::flush_streaming_text() {
  if (!has_pending_stream_ || !streaming_started_) {
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
}

void ConversationView::set_failure_notice_handler(std::function<void()> handler) {
  if (notice_ != nullptr) {
    notice_->set_on_click(std::move(handler));
  }
}

void ConversationView::set_busy(bool busy) {
  if (send_button_ != nullptr) {
    send_button_->setEnabled(!busy);
  }

  if (busy_ == busy) {
    return;
  }

  busy_ = busy;
  cancel_button_->setVisible(busy);

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
    send_button_->setToolTip(trs("Send"));
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
  if (controller_.busy()) {
    return;
  }

  const QString text = input_->toPlainText().trimmed();

  if (text.isEmpty()) {
    return;
  }

  transcript_->scroll_to_bottom();

  if (controller_.send(text.toStdString())) {
    input_->clear();
  }

  set_busy(controller_.busy());
}

} // namespace ui
