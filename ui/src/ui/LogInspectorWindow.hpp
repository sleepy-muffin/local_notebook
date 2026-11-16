#pragma once

// A live view of everything the application logs through felis::log. The status
// bar opens a single instance of this window; while it exists it holds a callback
// on the logger and appends each record to a bounded history.
//
// The logger invokes callbacks on its own worker thread, so the callback only
// deposits records into a shared, mutex-guarded sink. A timer drains that sink on
// the UI thread and builds the widget rows there.

#include <QDialog>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <felis/log.hpp>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QTimer;

namespace ui {

class LogInspectorWindow : public QDialog {
public:
  explicit LogInspectorWindow(QWidget* parent = nullptr);
  ~LogInspectorWindow() override;

protected:
  void showEvent(QShowEvent* event) override;

private:
  struct Record {
    felis::log::message_type type = felis::log::message_type::INFO;
    QString time;
    QString body;
    // Dumped request bodies arrive through log_payload_bridge rather than the
    // ordinary log stream; they carry a one-line title and are labelled
    // separately in the list.
    bool payload = false;
    QString title;
  };

  // Shared with the logger callback. Keeping the data behind a shared_ptr (and
  // an `alive` flag) means a callback still running on the worker thread when
  // this window is destroyed touches only the sink, never the window.
  struct Sink {
    std::mutex mutex;
    std::deque<Record> pending;
    bool alive = true;
  };

  void drain();
  void append_row(const Record& record);
  void apply_filter();
  void show_selected();
  void clear_records();
  void copy_selected();
  void apply_style();
  bool visible_for_filter(felis::log::message_type type) const;

  felis::log::callback_id callback_id_ = 0;
  std::shared_ptr<Sink> sink_;

  // Bounded history, oldest first. `list_` has exactly one row per record (in
  // the same order); the active filter only hides rows, so an index is stable.
  std::deque<Record> records_;

  QListWidget* list_ = nullptr;
  QComboBox* filter_ = nullptr;
  QLabel* counts_ = nullptr;
  QLabel* detail_header_ = nullptr;
  QPlainTextEdit* detail_ = nullptr;
  QPushButton* copy_button_ = nullptr;
  QTimer* drain_timer_ = nullptr;
};

} // namespace ui
