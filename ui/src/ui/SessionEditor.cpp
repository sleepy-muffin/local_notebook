#include "ui/SessionEditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <optional>
#include <string>
#include <vector>

#include "client.hpp"
#include "message.hpp"
#include "support/i18n.hpp"
#include "ui/SpinBox.hpp"
#include "ui/dialog_style.hpp"

namespace ui {

using namespace dialog_style;

namespace {

constexpr std::uint32_t kOnline = 1u << 0;
constexpr std::uint32_t kRead = 1u << 1;
constexpr std::uint32_t kWrite = 1u << 2;
constexpr std::uint32_t kBash = 1u << 3;

// Starting points for the session budgets (design-1.md §8), one row per load
// tier. Selecting a preset only fills the fields below; it never saves, so the
// numbers can still be tweaked before Save. threshold/preserve/summary map onto
// the compaction policy; response is the per-reply budget.
struct BudgetPreset {
  const char* name;
  int threshold;
  int preserve;
  int summary_budget;
  int response_budget;
};

constexpr BudgetPreset kBudgetPresets[] = {
    // Each row is a starting point for a model's context window. The trigger
    // sits at roughly half the window rather than at its edge, so the preserved
    // tail and a full reply still fit above it; the preserved tail is about a
    // tenth of the trigger and the summary budget a little larger, enough to
    // carry tool output and file paths forward. Picking a preset never saves.
    {"Instant",       16'000,   2'000,   4'000,   4'000}, //  32k window
    {"Very Light",    32'000,   4'000,   6'000,   6'000}, //  64k window
    {"Light",         64'000,   8'000,   8'000,  12'000}, // 128k window
    {"Balanced",      96'000,  12'000,  12'000,  16'000}, // 200k window
    {"Heavy",        192'000,  20'000,  20'000,  24'000}, // 400k window
    {"Very Heavy",   384'000,  32'000,  32'000,  32'000}, //   1M window
    {"Ultra",        768'000,  64'000,  48'000,  48'000}, //   2M window
};

} // namespace

SessionEditor::SessionEditor(
    felis::bell::client& api, std::uint32_t session_id, QWidget* parent
)
    : QDialog(parent), api_(api), session_id_(session_id) {
  setObjectName(QStringLiteral("SessionEditor"));
  setWindowTitle(session_id == 0 ? trs("New Session") : trs("Edit Session"));
  resize(760, 620);
  setMinimumSize(600, 440);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  nav_ = new QListWidget(this);
  nav_->setObjectName(QStringLiteral("SessionNav"));
  nav_->setFixedWidth(180);
  nav_->setFrameShape(QFrame::NoFrame);
  nav_->addItem(trs("Common"));
  nav_->addItem(trs("Custom Instructions"));
  nav_->addItem(trs("Tools"));
  style_navigation_rail(nav_);
  root->addWidget(nav_);
  root->addWidget(v_separator(this));

  auto* right = new QVBoxLayout();
  right->setContentsMargins(0, 0, 0, 16);
  right->setSpacing(0);

  stack_ = new QStackedWidget(this);
  stack_->setObjectName(QStringLiteral("SessionStack"));
  stack_->setFrameShape(QFrame::NoFrame);
  stack_->addWidget(build_common_tab());
  stack_->addWidget(build_instructions_tab());
  stack_->addWidget(build_tools_tab());
  right->addWidget(stack_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("Secondary"));
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() { save_session(); });
  connect(buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });

  auto* footer = new QHBoxLayout();
  footer->setContentsMargins(20, 0, 20, 0);
  footer->setSpacing(8);
  footer->addStretch(1);
  footer->addWidget(buttons);
  right->addLayout(footer);

  root->addLayout(right, 1);

  connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0) {
      stack_->setCurrentIndex(row);
    }
  });
  nav_->setCurrentRow(0);

  setStyleSheet(dialog_stylesheet(QStringLiteral("SessionEditor")));

  load_session();
}

QWidget* SessionEditor::page(
    const QString& title, const QString& subtitle, QWidget* content) {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(20, 14, 20, 14);
  layout->setSpacing(10);

  auto* heading = new QLabel(title, page);
  heading->setObjectName(QStringLiteral("PageTitle"));
  layout->addWidget(heading);

  if (!subtitle.isEmpty()) {
    auto* sub = new QLabel(subtitle, page);
    sub->setObjectName(QStringLiteral("PageSubtitle"));
    sub->setWordWrap(true);
    layout->addWidget(sub);
  }

  layout->addWidget(content, 1);
  return page;
}

QWidget* SessionEditor::scrolled_tab(QWidget* content) {
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(content);
  return scroll;
}

QWidget* SessionEditor::build_common_tab() {
  auto* content = new QWidget();
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 12, 0);
  layout->setSpacing(12);

  // ---- Session ----
  layout->addWidget(section_label(content, trs("Session")));
  {
    auto* form = form_layout(content);

    title_ = new QLineEdit(content);
    title_->setPlaceholderText(trs("Session title"));

    reasoning_ = new ComboBox(content);
    reasoning_->setEditable(true);
    reasoning_->addItems({QStringLiteral("none"), QStringLiteral("low"),
                          QStringLiteral("medium"), QStringLiteral("high")});
    reasoning_->setToolTip(trs("The reasoning level this session requests for chat turns."));

    // The reasoning level used when this session's inline summary is regenerated.
    // It is separate from the chat's `Reasoning` above so a reasoning model can
    // summarise at a lower effort (usually "none") and still emit visible text.
    summary_reasoning_ = new ComboBox(content);
    summary_reasoning_->setEditable(true);
    summary_reasoning_->addItems({QStringLiteral("none"), QStringLiteral("low"),
                                  QStringLiteral("medium"), QStringLiteral("high")});
    summary_reasoning_->setToolTip(trs(
        "The reasoning level used when this session's summary is updated. "
        "Chat turns are unaffected."
    ));

    form->addRow(field_label(content, trs("Title")), title_);
    form->addRow(field_label(content, trs("Reasoning")), reasoning_);
    form->addRow(field_label(content, trs("Summarization reasoning")), summary_reasoning_);

    layout->addLayout(form);
  }

  layout->addWidget(h_separator(content));

  // ---- Budgets ----
  layout->addWidget(section_label(content, trs("Budgets")));
  {
    auto* form = form_layout(content);

    threshold_ = make_spin(content, 16000);
    threshold_->setRange(1, 2'000'000'000);
    preserve_ = make_spin(content, 4000);
    preserve_->setRange(1, 2'000'000'000);
    compact_budget_ = make_spin(content, 4000);
    compact_budget_->setRange(1, 2'000'000'000);
    response_budget_ = make_spin(content, 8000);
    response_budget_->setRange(1, 2'000'000'000);

    form->addRow(field_label(content, trs("Compact threshold")), threshold_);
    form->addRow(field_label(content, trs("Preserve tail")), preserve_);
    form->addRow(field_label(content, trs("Summary budget")), compact_budget_);
    form->addRow(field_label(content, trs("Response budget")), response_budget_);

    // A "Preset" row above the numbers: picking one fills the fields below with
    // a starting point for a given load tier. It never saves on its own, so the
    // values can still be tweaked before Save.
    budget_preset_ = new ComboBox(content);
    budget_preset_->addItem(trs("Apply a preset..."));
    for (const BudgetPreset& preset: kBudgetPresets) {
      budget_preset_->addItem(trs(preset.name));
    }
    budget_preset_->setToolTip(trs(
        "Fill the budget fields below with a starting point for a given load. "
        "Nothing is saved until you press Save."
    ));
    connect(
        budget_preset_, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
          if (index <= 0) {
            return;
          }
          const BudgetPreset& preset = kBudgetPresets[index - 1];
          threshold_->setValue(preset.threshold);
          preserve_->setValue(preset.preserve);
          compact_budget_->setValue(preset.summary_budget);
          response_budget_->setValue(preset.response_budget);
        }
    );
    auto* preset_row = new QHBoxLayout();
    preset_row->setSpacing(8);
    preset_row->addWidget(field_label(content, trs("Preset")));
    preset_row->addWidget(budget_preset_, 1);

    layout->addLayout(preset_row);
    layout->addLayout(form);
  }

  layout->addStretch(1);

  return page(
      trs("Common"),
      trs("Title, reasoning and the budgets this session runs with."),
      scrolled_tab(content));
}

QWidget* SessionEditor::build_instructions_tab() {
  // The instruction box is the page: it fills the whole right pane instead of
  // sitting in a form so a long set of instructions stays readable.
  instructions_ = new QPlainTextEdit();
  instructions_->setTabChangesFocus(true);
  instructions_->setPlaceholderText(trs(
      "Custom instructions for this session. Kept separate from the generated "
      "summary; never overwritten by it."
  ));

  return page(
      trs("Custom Instructions"),
      trs("Extra guidance sent with every turn of this session."), instructions_);
}

QWidget* SessionEditor::build_tools_tab() {
  auto* content = new QWidget();
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 12, 0);
  layout->setSpacing(12);

  // ---- Shared directories ----
  layout->addWidget(section_label(content, trs("Shared directories")));
  {
    auto* hint = new QLabel(
        trs("One directory per line. They are mounted into the session's tools."),
        content);
    hint->setObjectName(QStringLiteral("PageSubtitle"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    shared_directories_ = new PlainTextEdit(content);
    shared_directories_->setPlaceholderText(trs("One directory per line"));
    shared_directories_->setMinimumHeight(120);
    layout->addWidget(shared_directories_);
  }

  layout->addWidget(h_separator(content));

  // ---- Tool permissions ----
  layout->addWidget(section_label(content, trs("Tools")));
  {
    auto* form = form_layout(content);
    online_tools_ = new QCheckBox(trs("Online (web search)"), content);
    read_tools_ = new QCheckBox(trs("Read files"), content);
    write_tools_ = new QCheckBox(trs("Write files"), content);
    bash_tools_ = new QCheckBox(trs("Run commands"), content);
    form->addRow(QString(), online_tools_);
    form->addRow(QString(), read_tools_);
    form->addRow(QString(), write_tools_);
    form->addRow(QString(), bash_tools_);
    layout->addLayout(form);
  }

  layout->addStretch(1);

  return page(
      trs("Tools"),
      trs("Directories this session can read and the tools it may use."),
      scrolled_tab(content));
}

void SessionEditor::load_session() {
  if (session_id_ == 0) {
    reasoning_->setCurrentText(QStringLiteral("medium"));
    summary_reasoning_->setCurrentText(QStringLiteral("none"));
    return;
  }

  const std::optional<felis::bell::session> stored =
      api_.load_session(felis::bell::size_type(session_id_));

  if (!stored.has_value()) {
    return;
  }

  const felis::bell::session& session = *stored;

  title_->setText(to_qstring(session.title()));
  instructions_->setPlainText(to_qstring(session.custom_instructions()));

  std::string dirs;
  for (const std::string& directory: session.shared_directories()) {
    if (!dirs.empty()) {
      dirs.push_back('\n');
    }
    dirs += directory;
  }
  shared_directories_->setPlainText(to_qstring(dirs));

  reasoning_->setCurrentText(to_qstring(session.reasoning_effort()));
  summary_reasoning_->setCurrentText(to_qstring(session.auto_compact_reasoning_effort()));

  const std::uint32_t caps = session.tools().raw();
  online_tools_->setChecked((caps & kOnline) != 0);
  read_tools_->setChecked((caps & kRead) != 0);
  write_tools_->setChecked((caps & kWrite) != 0);
  bash_tools_->setChecked((caps & kBash) != 0);

  threshold_->setValue(static_cast<int>(session.auto_compact_threshold().get()));
  preserve_->setValue(static_cast<int>(session.auto_compact_preserve().get()));
  compact_budget_->setValue(static_cast<int>(session.auto_compact_budget().get()));
  response_budget_->setValue(static_cast<int>(session.response_budget().get()));
}

void SessionEditor::save_session() {
  using felis::bell::size_type;

  std::string title = title_->text().trimmed().toStdString();
  if (title.empty()) {
    title = trs("Untitled").toStdString();
  }

  // A brand-new session has no row yet; create it (title only) so it has an id.
  if (session_id_ == 0) {
    const size_type created =
        api_.create_session(felis::bell::shadowed_session(0, title));
    session_id_ = created.get();
  }

  const std::optional<felis::bell::session> stored = api_.load_session(size_type(session_id_));
  if (!stored.has_value()) {
    reject();
    return;
  }

  std::vector<std::string> directories;
  for (const QString& line: shared_directories_->toPlainText().split(QLatin1Char('\n'))) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) {
      directories.push_back(trimmed.toStdString());
    }
  }

  std::uint32_t caps = 0;
  if (online_tools_->isChecked()) caps |= kOnline;
  if (read_tools_->isChecked()) caps |= kRead;
  if (write_tools_->isChecked()) caps |= kWrite;
  if (bash_tools_->isChecked()) caps |= kBash;

  felis::bell::session updated(
      size_type(session_id_), title, instructions_->toPlainText().toStdString(),
      reasoning_->currentText().toStdString(), summary_reasoning_->currentText().toStdString(),
      std::move(directories), stored->kind(),
      stored->pinned(), felis::bell::tool_capabilities(caps),
      size_type(static_cast<std::uint32_t>(threshold_->value())),
      size_type(static_cast<std::uint32_t>(preserve_->value())),
      size_type(static_cast<std::uint32_t>(compact_budget_->value())),
      size_type(static_cast<std::uint32_t>(response_budget_->value()))
  );

  api_.save_session(updated);
  accept();
}

} // namespace ui
