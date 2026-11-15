#include "ui/SettingsWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <string>

#include "client.hpp"
#include "store.hpp"
#include "support/i18n.hpp"
#include "ui/AssistantForm.hpp"
#include "ui/SpinBox.hpp"
#include "ui/dialog_style.hpp"
#include "ui/theme.hpp"

namespace ui {

using namespace dialog_style;

namespace {

// Whether the two settings agree on everything the rendered theme depends on.
// Saving an unrelated field (a budget, a directory) must not trigger a full
// application restyle and background decode, which is what makes Save feel slow.
bool theme_inputs_equal(const lnb::Settings& a, const lnb::Settings& b) {
  return a.theme == b.theme && a.ui_font == b.ui_font &&
         a.ui_font_size == b.ui_font_size && a.font == b.font &&
         a.font_size == b.font_size && a.mono_font == b.mono_font &&
         a.mono_font_size == b.mono_font_size &&
         a.inline_code_scale == b.inline_code_scale &&
         a.tool_text_scale == b.tool_text_scale &&
         a.background_url == b.background_url &&
         a.background_override == b.background_override && a.metrics == b.metrics;
}

// Fills a font selector from a stored family. An empty family means "system
// default", shown as the application font so the field is never left blank.
void set_font_box(FontComboBox* box, const std::string& family) {
  box->setCurrentFont(
      family.empty() ? QApplication::font() : QFont(QString::fromStdString(family)));
}

// The family to persist for a selector whose stored value was `original`. When
// the stored value was empty ("system default") and the user left the selector
// on the application font, keep it empty rather than baking in a concrete family
// — for the monospace role that would replace the platform monospace font with a
// proportional one and break code rendering. (The monospace default is now a
// concrete family, so this only matters for older settings files.)
std::string stored_family(const FontComboBox* box, const std::string& original) {
  const QString family = box->currentFont().family();
  if (original.empty() && family == QApplication::font().family()) {
    return {};
  }
  return family.toStdString();
}

} // namespace

SettingsWindow::SettingsWindow(
    felis::bell::client& api, lnb::Settings& settings, QWidget* parent
)
    : QDialog(parent), api_(api), settings_(settings) {
  setObjectName(QStringLiteral("SettingsWindow"));
  setWindowTitle(trs("Settings"));
  resize(760, 620);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  nav_ = new QListWidget(this);
  nav_->setObjectName(QStringLiteral("SettingsNav"));
  nav_->setFixedWidth(180);
  nav_->addItem(trs("Configuration"));
  nav_->addItem(trs("Metrics"));
  nav_->addItem(trs("Assistant"));
  nav_->addItem(trs("About"));
  root->addWidget(nav_);

  auto* right = new QVBoxLayout();
  right->setContentsMargins(20, 20, 20, 16);
  right->setSpacing(12);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(build_config_page());
  stack_->addWidget(build_metrics_page());
  stack_->addWidget(build_assistant_page());
  stack_->addWidget(build_about_page());
  right->addWidget(stack_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("Secondary"));
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() { save(); });
  connect(buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });
  right->addWidget(buttons);

  root->addLayout(right, 1);

  connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0) {
      stack_->setCurrentIndex(row);
    }
  });
  nav_->setCurrentRow(0);

  apply_style();
}

QWidget* SettingsWindow::make_page(
    const QString& title, const QString& subtitle, QWidget* content
) {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* heading = new QLabel(title, page);
  heading->setObjectName(QStringLiteral("PageTitle"));
  apply_derived_font(heading, 1.0, true);
  layout->addWidget(heading);

  if (!subtitle.isEmpty()) {
    auto* sub = new QLabel(subtitle, page);
    sub->setObjectName(QStringLiteral("PageSubtitle"));
    sub->setWordWrap(true);
    layout->addWidget(sub);
  }

  auto* scroll = new QScrollArea(page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);
  layout->addWidget(scroll, 1);

  return page;
}

QWidget* SettingsWindow::build_config_page() {
  auto* content = new QWidget(this);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  // ---- Appearance ----
  layout->addWidget(section_label(content, trs("Appearance")));
  {
    auto* form = form_layout(content);

    theme_ = new ComboBox(content);
    for (const QString& name: theme_names()) {
      theme_->addItem(name);
    }

    language_ = new ComboBox(content);
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->addItem(QStringLiteral("\u4e2d\u6587"), QStringLiteral("zh"));
    language_->addItem(QStringLiteral("\u65e5\u672c\u8a9e"), QStringLiteral("ja"));

    form->addRow(field_label(content, trs("Theme")), theme_);
    form->addRow(field_label(content, trs("Language")), language_);

    layout->addLayout(form);
  }

  layout->addWidget(h_separator(content));

  // ---- Typography ----
  // Interface (every widget), chat (transcript + composer) and monospace (code)
  // families each pair a selector with a point size; inline code is scaled from
  // the chat font so it never looks oversized next to the prose.
  layout->addWidget(section_label(content, trs("Typography")));
  {
    auto* form = form_layout(content);

    // A point-size spin box: unlike make_spin()'s budget range, this is a small
    // 6..48 pt range stepping by one.
    const auto make_size = [content](int value) {
      auto* spin = new SpinBox(content);
      spin->setRange(6, 48);
      spin->setSingleStep(1);
      spin->setValue(value);
      return spin;
    };

    ui_font_ = new FontComboBox(content);
    ui_font_size_ = make_size(settings_.ui_font_size);
    font_ = new FontComboBox(content);
    font_size_ = make_size(settings_.font_size);
    mono_font_ = new FontComboBox(content);
    mono_font_size_ = make_size(settings_.mono_font_size);

    inline_code_scale_ = new DoubleSpinBox(content);
    inline_code_scale_->setRange(0.50, 1.50);
    inline_code_scale_->setSingleStep(0.05);
    inline_code_scale_->setDecimals(2);
    inline_code_scale_->setValue(static_cast<double>(settings_.inline_code_scale));
    inline_code_scale_->setToolTip(
        trs("Inline code size as a fraction of the surrounding text.")
    );

    tool_text_scale_ = new DoubleSpinBox(content);
    tool_text_scale_->setRange(0.50, 1.50);
    tool_text_scale_->setSingleStep(0.05);
    tool_text_scale_->setDecimals(2);
    tool_text_scale_->setValue(static_cast<double>(settings_.tool_text_scale));
    tool_text_scale_->setToolTip(
        trs("Tool call and result size as a fraction of the message text.")
    );

    form->addRow(field_label(content, trs("Interface font")), ui_font_);
    form->addRow(field_label(content, trs("Interface size")), ui_font_size_);
    form->addRow(field_label(content, trs("Chat font")), font_);
    form->addRow(field_label(content, trs("Chat size")), font_size_);
    form->addRow(field_label(content, trs("Monospace font")), mono_font_);
    form->addRow(field_label(content, trs("Monospace size")), mono_font_size_);
    form->addRow(field_label(content, trs("Inline code size")), inline_code_scale_);
    form->addRow(field_label(content, trs("Tool strip size")), tool_text_scale_);

    layout->addLayout(form);
  }

  layout->addWidget(h_separator(content));

  // ---- Composer ----
  layout->addWidget(section_label(content, trs("Composer")));
  {
    auto* form = form_layout(content);

    enter_modifier_ = new QCheckBox(trs("Ctrl+Enter sends; Enter inserts a newline"), content);

    rendered_limit_ = make_spin(content, 20);
    rendered_limit_->setRange(1, 10000);
    history_page_ = make_spin(content, 20);
    history_page_->setRange(1, 10000);

    form->addRow(QString(), enter_modifier_);
    form->addRow(field_label(content, trs("Rendered messages")), rendered_limit_);
    form->addRow(field_label(content, trs("History page size")), history_page_);

    layout->addLayout(form);
  }

  layout->addWidget(h_separator(content));

  // ---- Startup ----
  // Auto-login is what skips the picker at launch. Because turning it on hides
  // the picker — the only other place to change it — it must also be reachable
  // here so it can be switched back off without editing settings.json by hand.
  layout->addWidget(section_label(content, trs("Startup")));
  {
    auto_login_ = new QCheckBox(
        trs("Open the current assistant automatically on next launch"), content
    );
    auto_login_->setToolTip(
        trs("When off, the assistant picker is shown at every launch.")
    );
    layout->addWidget(auto_login_);
  }

  layout->addStretch(1);

  return make_page(
      trs("Configuration"), trs("Appearance and startup behaviour for the whole app."),
      content);
}

QWidget* SettingsWindow::build_metrics_page() {
  auto* content = new QWidget(this);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  // Registers one spin box for a ThemeMetrics field. The value is filled in by
  // reload_metrics(); save() reads the boxes back into settings_.metrics.
  const auto add_metric = [this, content](
                              QFormLayout* target, const QString& label, int minimum,
                              int maximum, int step, int lnb::ThemeMetrics::*field
                          ) {
    QSpinBox* spin = make_spin(content, 0);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setMinimumWidth(76);
    target->addRow(field_label(content, label), spin);
    metric_fields_.emplace_back(spin, field);
  };

  layout->addWidget(section_label(content, trs("Layout")));
  auto* form = form_layout(content);

  add_metric(form, trs("Sidebar width"), 120, 800, 4, &lnb::ThemeMetrics::sidebar_width);
  add_metric(form, trs("Task list width"), 160, 800, 4, &lnb::ThemeMetrics::tasks_list_width);
  add_metric(form, trs("Page margin"), 0, 200, 1, &lnb::ThemeMetrics::page_margin);
  add_metric(form, trs("Message spacing"), 0, 200, 1, &lnb::ThemeMetrics::message_spacing);
  add_metric(form, trs("Composer height"), 32, 400, 2, &lnb::ThemeMetrics::composer_height);
  add_metric(
      form, trs("Expanded height"), 32, 800, 2, &lnb::ThemeMetrics::composer_height_expanded);
  add_metric(
      form, trs("Session item height"), 20, 120, 2, &lnb::ThemeMetrics::session_item_height);
  add_metric(form, trs("Title font size"), 6, 48, 1, &lnb::ThemeMetrics::title_font_size);
  add_metric(form, trs("Flow max width"), 0, 4000, 10, &lnb::ThemeMetrics::message_max_width);
  add_metric(form, trs("Bubble radius"), 0, 40, 1, &lnb::ThemeMetrics::bubble_radius);

  layout->addLayout(form);
  layout->addWidget(h_separator(content));

  layout->addWidget(section_label(content, trs("Reading rhythm")));
  auto* hint = new QLabel(trs("Line heights are percentages (100 is single spacing)."), content);
  hint->setObjectName(QStringLiteral("Muted"));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto* rhythm = form_layout(content);
  add_metric(
      rhythm, trs("Assistant line height"), 80, 300, 5, &lnb::ThemeMetrics::line_height_other);
  add_metric(
      rhythm, trs("User line height"), 80, 300, 5, &lnb::ThemeMetrics::line_height_self);
  add_metric(
      rhythm, trs("Paragraph spacing"), 0, 80, 1, &lnb::ThemeMetrics::paragraph_spacing);
  add_metric(
      rhythm, trs("List spacing"), 0, 80, 1, &lnb::ThemeMetrics::list_paragraph_spacing);
  layout->addLayout(rhythm);

  {
    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    actions->addStretch(1);

    auto* restore = new QPushButton(trs("Restore to defaults"), content);
    restore->setObjectName(QStringLiteral("Secondary"));
    restore->setToolTip(trs(
        "Reset these metrics to the current theme's own defaults. Nothing is "
        "saved until you press Save."
    ));
    connect(restore, &QPushButton::clicked, this, [this]() { restore_metrics_defaults(); });
    actions->addWidget(restore);
    layout->addLayout(actions);
  }

  layout->addStretch(1);

  return make_page(
      trs("Metrics"), trs("Sizing and spacing of the interface."), content);
}

QWidget* SettingsWindow::build_assistant_page() {
  auto* content = new QWidget(this);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  assistant_form_ = new AssistantForm(content);
  layout->addWidget(assistant_form_);

  layout->addWidget(section_label(content, trs("Assistant memory")));
  assistant_memory_ = new PlainTextEdit(content);
  assistant_memory_->setMinimumHeight(120);
  layout->addWidget(assistant_memory_);
  layout->addStretch(1);

  return make_page(
      trs("Assistant"), trs("Configuration of the open assistant."), content);
}

QWidget* SettingsWindow::build_about_page() {
  auto* content = new QWidget(this);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* body = new QLabel(
      trs("Local Notebook\nA local-first notebook for chat assistants."), content);
  body->setWordWrap(true);
  layout->addWidget(body);
  layout->addStretch(1);

  return make_page(trs("About"), QString(), content);
}

void SettingsWindow::apply_style() {
  setStyleSheet(dialog_stylesheet(QStringLiteral("SettingsWindow")));
}

void SettingsWindow::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  apply_style();
  reload_from_app();
}

void SettingsWindow::reload_from_app() {
  const int theme_index = theme_->findText(to_qstring(settings_.theme));
  if (theme_index >= 0) {
    theme_->setCurrentIndex(theme_index);
  }

  const int lang_index = language_->findData(to_qstring(settings_.language));
  if (lang_index >= 0) {
    language_->setCurrentIndex(lang_index);
  }

  enter_modifier_->setChecked(settings_.enter_with_modifier);
  rendered_limit_->setValue(settings_.rendered_message_limit);
  history_page_->setValue(settings_.history_page_size);
  auto_login_->setChecked(settings_.auto_login);

  set_font_box(ui_font_, settings_.ui_font);
  ui_font_size_->setValue(settings_.ui_font_size);
  set_font_box(font_, settings_.font);
  font_size_->setValue(settings_.font_size);
  set_font_box(mono_font_, settings_.mono_font);
  mono_font_size_->setValue(settings_.mono_font_size);
  inline_code_scale_->setValue(static_cast<double>(settings_.inline_code_scale));
  tool_text_scale_->setValue(static_cast<double>(settings_.tool_text_scale));

  if (assistant_form_ != nullptr) {
    assistant_form_->set_assistant(api_.current_assistant());
  }
  if (assistant_memory_ != nullptr) {
    assistant_memory_->setPlainText(to_qstring(api_.current_assistant().memory_));
  }

  reload_metrics();
}

void SettingsWindow::reload_metrics() {
  // Show the effective metrics (theme defaults with any stored override applied),
  // so the fields always reflect what the interface is actually using.
  const lnb::ThemeMetrics& effective = metrics();
  for (auto& [spin, field]: metric_fields_) {
    spin->setValue(effective.*field);
  }
}

void SettingsWindow::restore_metrics_defaults() {
  // Reset to the current theme's own defaults (before any user override). This
  // only fills the boxes; nothing is persisted until Save.
  const lnb::ThemeMetrics& defaults = theme().metrics;
  for (auto& [spin, field]: metric_fields_) {
    spin->setValue(defaults.*field);
  }
}

void SettingsWindow::save() {
  // Captured before the edits, so we can tell whether anything the theme depends
  // on changed and only restyle the running app when it actually did.
  const lnb::Settings previous = settings_;

  settings_.theme = theme_->currentText().toStdString();
  const QString language = language_->currentData().toString();
  if (!language.isEmpty()) {
    settings_.language = language.toStdString();
  }
  settings_.enter_with_modifier = enter_modifier_->isChecked();
  settings_.rendered_message_limit = rendered_limit_->value();
  settings_.history_page_size = history_page_->value();
  settings_.auto_login = auto_login_->isChecked();

  // `stored_family` keeps an unchanged "system default" selector empty instead
  // of baking in the application font; ui_font has a concrete Carlito default, so
  // it always persists its current family.
  settings_.ui_font = stored_family(ui_font_, settings_.ui_font);
  settings_.ui_font_size = ui_font_size_->value();
  settings_.font = stored_family(font_, settings_.font);
  settings_.font_size = font_size_->value();
  settings_.mono_font = stored_family(mono_font_, settings_.mono_font);
  settings_.mono_font_size = mono_font_size_->value();
  settings_.inline_code_scale = static_cast<float>(inline_code_scale_->value());
  settings_.tool_text_scale = static_cast<float>(tool_text_scale_->value());

  // Write the metric boxes back into a full ThemeMetrics and store it as the
  // override, so the layout knobs actually persist (nullopt would mean "use the
  // theme defaults" and silently drop the user's edits).
  lnb::ThemeMetrics metrics_override = metrics();
  for (const auto& [spin, field]: metric_fields_) {
    metrics_override.*field = spin->value();
  }
  settings_.metrics = metrics_override;

  if (assistant_form_ != nullptr) {
    felis::bell::assistant assistant = api_.current_assistant();
    assistant_form_->write_into(assistant);
    if (assistant_memory_ != nullptr) {
      assistant.memory_ = assistant_memory_->toPlainText().toStdString();
    }
    api_.set_assistant(assistant);
  }

  lnb::save_settings(api_.get_store(), settings_);

  // Re-apply the theme at runtime so a theme/metric/font change is visible
  // immediately instead of only after a restart. This rebuilds the application
  // palette, stylesheet, fonts and background, so it is gated on an actual
  // change to keep an unrelated Save (a budget, a directory) cheap.
  if (!theme_inputs_equal(previous, settings_)) {
    if (auto* application = qobject_cast<QApplication*>(QApplication::instance())) {
      ui::apply_theme(*application, settings_);
    }
  }

  if (on_saved_) {
    on_saved_();
  }

  accept();
}

} // namespace ui
