#include "ui/LogInspectorWindow.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "support/LlmPayloadFormat.hpp"
#include "support/LogFormat.hpp"
#include "support/LogPayloadBridge.hpp"
#include "support/i18n.hpp"
#include "ui/LogRowDelegate.hpp"
#include "ui/dialog_style.hpp"
#include "ui/theme.hpp"

namespace ui {

namespace log_inspector_detail {

constexpr int kMaxRecords = 2000;
constexpr int kDrainIntervalMs = 120;

} // namespace log_inspector_detail

using namespace dialog_style;
using namespace log_inspector_detail;

LogInspectorWindow::LogInspectorWindow(QWidget* parent)
    : QDialog(parent), sink_(std::make_shared<Sink>()) {
  setObjectName(QStringLiteral("LogInspectorWindow"));
  setWindowTitle(trs("Logs"));
  setModal(false);
  resize(880, 560);
  setMinimumSize(560, 320);
  setFont(ui_font());

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 20, 20, 16);
  root->setSpacing(12);

  auto* title = new QLabel(trs("Logs"), this);
  title->setObjectName(QStringLiteral("DialogTitle"));
  apply_derived_font(title, 2.0, true);
  root->addWidget(title);

  auto* subtitle =
      new QLabel(trs("Live records from the running application."), this);
  subtitle->setObjectName(QStringLiteral("DialogSubtitle"));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  controls->addWidget(field_label(this, trs("Level")));

  filter_ = new QComboBox(this);
  filter_->addItem(trs("All levels"), -1);
  filter_->addItem(
      trs("Info"), static_cast<int>(felis::log::message_type::INFO));
  filter_->addItem(
      trs("Debug"), static_cast<int>(felis::log::message_type::DEBUG));
  filter_->addItem(
      trs("Error"), static_cast<int>(felis::log::message_type::ERROR));
  filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  filter_->setStyleSheet(
      QStringLiteral("QComboBox::drop-down { width: 0px; border: none; }"
                     "QComboBox::down-arrow { image: none; }"));
  controls->addWidget(filter_);

  controls->addStretch(1);

  counts_ = new QLabel(this);
  counts_->setObjectName(QStringLiteral("Muted"));
  controls->addWidget(counts_);

  root->addLayout(controls);

  auto* split = new QSplitter(Qt::Horizontal, this);
  split->setChildrenCollapsible(false);
  split->setHandleWidth(8);

  list_ = new QListWidget(split);
  list_->setObjectName(QStringLiteral("LogList"));
  list_->setFont(ui_font());
  list_->setSpacing(1);
  list_->setFrameShape(QFrame::NoFrame);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setWordWrap(false);
  list_->setUniformItemSizes(true);
  show_parent_background(list_);
  list_->setItemDelegate(new LogRowDelegate(list_));
  split->addWidget(list_);

  auto* right = new QWidget(split);
  auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(12, 0, 0, 0);
  right_layout->setSpacing(8);

  detail_header_ = new QLabel(right);
  detail_header_->setObjectName(QStringLiteral("FieldLabel"));
  right_layout->addWidget(detail_header_);

  detail_ = new QPlainTextEdit(right);
  detail_->setObjectName(QStringLiteral("LogDetail"));
  detail_->setReadOnly(true);
  detail_->setLineWrapMode(QPlainTextEdit::NoWrap);
  detail_->setFont(mono_font());
  detail_->setPlaceholderText(trs("Select a log entry to read it."));
  right_layout->addWidget(detail_, 1);

  split->addWidget(right);
  split->setStretchFactor(0, 2);
  split->setStretchFactor(1, 3);
  root->addWidget(split, 1);

  auto* footer = new QHBoxLayout();
  footer->setContentsMargins(0, 0, 0, 0);
  footer->setSpacing(8);
  footer->addStretch(1);

  auto* clear_button = new QPushButton(trs("Clear"), this);
  clear_button->setObjectName(QStringLiteral("Secondary"));
  connect(clear_button, &QPushButton::clicked, this, [this]() { clear_records(); });
  footer->addWidget(clear_button);

  copy_button_ = new QPushButton(trs("Copy entry"), this);
  copy_button_->setObjectName(QStringLiteral("Secondary"));
  copy_button_->setEnabled(false);
  connect(copy_button_, &QPushButton::clicked, this, [this]() { copy_selected(); });
  footer->addWidget(copy_button_);

  root->addLayout(footer);

  connect(
      list_, &QListWidget::itemSelectionChanged, this, [this]() { show_selected(); });
  connect(
      filter_, &QComboBox::currentIndexChanged, this, [this](int) { apply_filter(); });

  drain_timer_ = new QTimer(this);
  connect(drain_timer_, &QTimer::timeout, this, [this]() { drain(); });
  drain_timer_->start(kDrainIntervalMs);

  apply_style();
  counts_->setText(trs("%1 records").arg(0));

  // The callback runs on the logger's worker thread. It captures only the sink,
  // never `this`, so it stays valid even if it fires while this window is being
  // torn down.
  std::shared_ptr<Sink> sink = sink_;
  callback_id_ = felis::log::register_callback(
      [sink](felis::log::message_type type,
             std::string& content) -> felis::log::logging_callback_result {
        std::lock_guard lock(sink->mutex);

        if (!sink->alive) {
          return felis::log::logging_callback_result::PASS;
        }

        Record record;
        record.type = type;
        record.time = log_format::current_timestamp();
        record.body = QString::fromStdString(content);
        sink->pending.push_back(std::move(record));

        while (sink->pending.size() > kMaxRecords) {
          sink->pending.pop_front();
        }

        return felis::log::logging_callback_result::PASS;
      }
  );
}

LogInspectorWindow::~LogInspectorWindow() {
  if (callback_id_ != 0) {
    felis::log::unregister_callback(callback_id_);
  }

  // A callback may already have been copied out by the logger and still be
  // running. Taking the same lock makes this wait for it, then marks the sink
  // dead so any late arrival is ignored.
  std::lock_guard lock(sink_->mutex);
  sink_->alive = false;
}

void LogInspectorWindow::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  apply_style();
}

void LogInspectorWindow::drain() {
  std::deque<Record> incoming;
  {
    std::lock_guard lock(sink_->mutex);
    incoming.swap(sink_->pending);
  }

  // Payload dumps never travel through the ordinary log stream: the bridge
  // terminates them before the file can see them and queues a rendered copy.
  for (log_payload_bridge::Payload& payload : log_payload_bridge::take_pending()) {
    Record record;
    record.time = payload.time;
    record.body = payload.body;
    record.title = payload.title;
    record.payload = true;
    incoming.push_back(std::move(record));
  }

  if (incoming.empty()) {
    return;
  }

  for (const Record& record : incoming) {
    records_.push_back(record);
    append_row(record);
  }

  while (records_.size() > kMaxRecords) {
    records_.pop_front();

    if (list_->count() > 0) {
      delete list_->takeItem(0);
    }
  }

  counts_->setText(trs("%1 records").arg(records_.size()));
}

void LogInspectorWindow::append_row(const Record& record) {
  auto* item = new QListWidgetItem(list_);
  item->setData(log_roles::time, record.time);
  item->setData(
      log_roles::level,
      record.payload ? trs("Payload") : log_format::level_name(record.type)
  );
  item->setData(
      log_roles::preview,
      record.payload ? record.title : log_format::preview(record.body)
  );
  item->setData(log_roles::severity, static_cast<int>(record.type));
  item->setData(log_roles::payload, record.payload);
  item->setHidden(!visible_for_filter(record.type));
}

bool LogInspectorWindow::visible_for_filter(felis::log::message_type type) const {
  const int wanted = filter_->currentData().toInt();
  return wanted < 0 || wanted == static_cast<int>(type);
}

void LogInspectorWindow::apply_filter() {
  const int rows = static_cast<int>(records_.size());

  for (int row = 0; row < list_->count() && row < rows; ++row) {
    if (QListWidgetItem* item = list_->item(row)) {
      item->setHidden(!visible_for_filter(records_[row].type));
    }
  }
}

void LogInspectorWindow::show_selected() {
  const int row = list_->currentRow();

  if (row < 0 || row >= static_cast<int>(records_.size())) {
    detail_header_->clear();
    detail_->clear();
    copy_button_->setEnabled(false);
    return;
  }

  const Record& record = records_[row];
  detail_header_->setText(
      trs("%1   ·   %2")
          .arg(
              record.payload ? trs("Payload") : log_format::level_name(record.type),
              record.time
          )
  );
  detail_->setPlainText(record.body);
  copy_button_->setEnabled(true);
}

void LogInspectorWindow::copy_selected() {
  if (detail_ == nullptr) {
    return;
  }

  QApplication::clipboard()->setText(detail_->toPlainText());
}

void LogInspectorWindow::clear_records() {
  records_.clear();
  list_->clear();
  detail_header_->clear();
  detail_->clear();
  copy_button_->setEnabled(false);
  counts_->setText(trs("%1 records").arg(0));

  // Drop whatever is still queued, so the list does not refill from records
  // captured a moment before the button was pressed.
  std::lock_guard lock(sink_->mutex);
  sink_->pending.clear();

  // Payloads captured since the last drain belong to the history we just wiped.
  log_payload_bridge::take_pending();
}

void LogInspectorWindow::apply_style() {
  setStyleSheet(dialog_stylesheet(QStringLiteral("LogInspectorWindow")));
  setFont(ui_font());

  if (list_ != nullptr) {
    list_->setFont(ui_font());
  }
  if (detail_ != nullptr) {
    detail_->setFont(mono_font());
  }

  reapply_derived_fonts(this);
}

} // namespace ui
