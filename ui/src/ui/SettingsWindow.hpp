#pragma once

#include <QDialog>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <felis/async.hpp>

#include "settings/metrics.hpp"
#include "settings/settings.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QWidget;

namespace felis::bell {
class client;
}

namespace ui {

class AssistantForm;
class FontComboBox;
class PlainTextEdit;

// A separate settings window: a left navigation with Configuration, Metrics,
// Assistant and About pages. Configuration edits the app-global struct; Metrics
// edits the layout knobs (data/settings.json "metrics"); Assistant edits the
// open assistant's own configuration. Save persists everything and closes.
class SettingsWindow : public QDialog {
public:
  explicit SettingsWindow(
      felis::bell::client& api, lnb::Settings& settings,
      felis::async::multi_executor<void()>& executor, QWidget* parent = nullptr
  );
  ~SettingsWindow() override;

  // Kept for the caller's convenience; the current session is shown on About.
  void set_current_session(std::uint32_t session_id) { current_session_ = session_id; }

  // Invoked after a successful Save, so the main window can restyle its chrome.
  void set_on_saved(std::function<void()> handler) {
    on_saved_ = std::move(handler);
  }

protected:
  void showEvent(QShowEvent* event) override;

private:
  QWidget* build_config_page();
  QWidget* build_metrics_page();
  QWidget* build_assistant_page();
  QWidget* build_about_page();
  QWidget* make_page(const QString& title, const QString& subtitle, QWidget* content);
  void apply_style();
  void reload_from_app();
  void reload_metrics();
  void restore_metrics_defaults();
  void save();

  // Starts one manual memory merge on the shared executor, and drains its result
  // on the GUI thread. The update is never automatic and never blocks the UI.
  void start_memory_update();
  void poll_memory_update();

  felis::bell::client& api_;
  lnb::Settings& settings_;
  felis::async::multi_executor<void()>& executor_;

  QListWidget* nav_ = nullptr;
  QStackedWidget* stack_ = nullptr;

  QComboBox* theme_ = nullptr;
  QComboBox* language_ = nullptr;
  // Startup behaviour: when checked, the current assistant is reopened on the
  // next launch instead of showing the picker. Available here as well as on the
  // picker so it can be turned back off once auto-login is already on.
  QCheckBox* auto_login_ = nullptr;
  QCheckBox* enter_modifier_ = nullptr;
  QSpinBox* rendered_limit_ = nullptr;
  QSpinBox* history_page_ = nullptr;

  // Typography. Each family selector maps to a Settings string (empty means the
  // system default) and each size to the matching point size; the inline-code
  // ratio scales monospace inside prose relative to the chat font.
  FontComboBox* ui_font_ = nullptr;
  QSpinBox* ui_font_size_ = nullptr;
  FontComboBox* font_ = nullptr;
  QSpinBox* font_size_ = nullptr;
  FontComboBox* mono_font_ = nullptr;
  QSpinBox* mono_font_size_ = nullptr;
  QDoubleSpinBox* inline_code_scale_ = nullptr;
  QDoubleSpinBox* tool_text_scale_ = nullptr;

  // One spin box per editable ThemeMetrics field, so load/save and "restore to
  // defaults" can walk the whole set without repeating the field list.
  std::vector<std::pair<QSpinBox*, int lnb::ThemeMetrics::*>> metric_fields_;

  AssistantForm* assistant_form_ = nullptr;
  PlainTextEdit* assistant_memory_ = nullptr;
  QPushButton* update_memory_ = nullptr;
  QLabel* memory_status_ = nullptr;
  QTimer* memory_poll_ = nullptr;

  // The worker only writes this record; `poll_memory_update` reads it on the GUI
  // thread. Shared by pointer so a result arriving after the window is gone lands
  // in live memory instead of a destroyed object.
  struct memory_update_state {
    std::mutex mutex;
    bool done = false;
    bool ok = false;
    std::string message;
  };
  std::shared_ptr<memory_update_state> memory_state_;
  felis::async::multi_executor<void()>::handle memory_handle_;

  std::uint32_t current_session_ = 0;
  std::function<void()> on_saved_;
};

} // namespace ui
