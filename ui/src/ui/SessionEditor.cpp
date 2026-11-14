#include "ui/SessionEditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
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
    {"Instant",      1000,   250,   250,   500},
    {"Very Light",   2000,   500,  1000,  1000},
    {"Light",        4000,  1000,  1200,  2000},
    {"Balanced",    12000,  2000,  4000,  4000},
    {"Heavy",       24000,  4000,  6000,  8000},
    {"Very Heavy",  48000,  8000,  8000, 16000},
    {"Ultra",       64000, 16000, 16000, 32000},
};

} // namespace

SessionEditor::SessionEditor(
    felis::bell::client& api, std::uint32_t session_id, QWidget* parent
)
    : QDialog(parent), api_(api), session_id_(session_id) {
  setObjectName(QStringLiteral("SessionEditor"));
  setWindowTitle(session_id == 0 ? trs("New Session") : trs("Edit Session"));
  resize(560, 640);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  auto* form = form_layout(this);

  title_ = new QLineEdit(this);
  title_->setPlaceholderText(trs("Session title"));

  instructions_ = new PlainTextEdit(this);
  instructions_->setPlaceholderText(trs("Custom instructions for this session"));
  instructions_->setMinimumHeight(96);

  shared_directories_ = new PlainTextEdit(this);
  shared_directories_->setPlaceholderText(trs("One directory per line"));
  shared_directories_->setMinimumHeight(72);

  form->addRow(field_label(this, trs("Title")), title_);
  form->addRow(field_label(this, trs("Instructions")), instructions_);
  form->addRow(field_label(this, trs("Shared directories")), shared_directories_);

  reasoning_ = new ComboBox(this);
  reasoning_->setEditable(true);
  reasoning_->addItems({QStringLiteral("none"), QStringLiteral("low"), QStringLiteral("medium"),
                        QStringLiteral("high")});
  form->addRow(field_label(this, trs("Reasoning")), reasoning_);

  // The reasoning level used when this session's inline summary is regenerated.
  // It is separate from the chat's `Reasoning` above so a reasoning model can
  // summarise at a lower effort (usually "none") and still emit visible text.
  summary_reasoning_ = new ComboBox(this);
  summary_reasoning_->setEditable(true);
  summary_reasoning_->addItems({QStringLiteral("none"), QStringLiteral("low"),
                                QStringLiteral("medium"), QStringLiteral("high")});
  summary_reasoning_->setToolTip(trs(
      "The reasoning level used when this session's summary is updated. "
      "Chat turns are unaffected."
  ));
  form->addRow(field_label(this, trs("Summarization reasoning")), summary_reasoning_);

  layout->addLayout(form);

  // Tool permissions.
  layout->addWidget(section_label(this, trs("Tools")));
  {
    auto* tools_form = form_layout(this);
    online_tools_ = new QCheckBox(trs("Online (web search)"), this);
    read_tools_ = new QCheckBox(trs("Read files"), this);
    write_tools_ = new QCheckBox(trs("Write files"), this);
    bash_tools_ = new QCheckBox(trs("Run commands"), this);
    tools_form->addRow(QString(), online_tools_);
    tools_form->addRow(QString(), read_tools_);
    tools_form->addRow(QString(), write_tools_);
    tools_form->addRow(QString(), bash_tools_);
    layout->addLayout(tools_form);
  }

  // Budgets.
  layout->addWidget(section_label(this, trs("Budgets")));
  {
    auto* budget_form = form_layout(this);
    threshold_ = make_spin(this, 16000);
    threshold_->setRange(1, 2'000'000'000);
    preserve_ = make_spin(this, 4000);
    preserve_->setRange(1, 2'000'000'000);
    compact_budget_ = make_spin(this, 4000);
    compact_budget_->setRange(1, 2'000'000'000);
    response_budget_ = make_spin(this, 8000);
    response_budget_->setRange(1, 2'000'000'000);

    budget_form->addRow(field_label(this, trs("Compact threshold")), threshold_);
    budget_form->addRow(field_label(this, trs("Preserve tail")), preserve_);
    budget_form->addRow(field_label(this, trs("Summary budget")), compact_budget_);
    budget_form->addRow(field_label(this, trs("Response budget")), response_budget_);

    // A "Preset" row above the numbers: picking one fills the fields below with
    // a starting point for a given load tier. It never saves on its own, so the
    // values can still be tweaked before Save.
    budget_preset_ = new ComboBox(this);
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
    preset_row->addWidget(field_label(this, trs("Preset")));
    preset_row->addWidget(budget_preset_, 1);

    layout->addLayout(preset_row);
    layout->addLayout(budget_form);
  }

  layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
  buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("Secondary"));
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() { save_session(); });
  connect(buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });
  layout->addWidget(buttons);

  setStyleSheet(dialog_stylesheet(QStringLiteral("SessionEditor")));

  load_session();
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
