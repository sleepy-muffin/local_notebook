#pragma once

#include <QDialog>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "settings/metrics.hpp"
#include "settings/settings.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QShowEvent;
class QSpinBox;
class QStackedWidget;
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
      felis::bell::client& api, lnb::Settings& settings, QWidget* parent = nullptr
  );

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

  felis::bell::client& api_;
  lnb::Settings& settings_;

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

  // One spin box per editable ThemeMetrics field, so load/save and "restore to
  // defaults" can walk the whole set without repeating the field list.
  std::vector<std::pair<QSpinBox*, int lnb::ThemeMetrics::*>> metric_fields_;

  AssistantForm* assistant_form_ = nullptr;
  PlainTextEdit* assistant_memory_ = nullptr;

  std::uint32_t current_session_ = 0;
  std::function<void()> on_saved_;
};

} // namespace ui
