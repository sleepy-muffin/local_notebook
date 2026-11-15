#include "ui/AssistantForm.hpp"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstddef>
#include <optional>
#include <string>

#include "support/i18n.hpp"
#include "ui/dialog_style.hpp"

namespace ui {

using namespace dialog_style;

namespace {

struct SummaryPreset {
  const char* name;
  int memory_budget;
  int session_pool;
  int message_pool;
};

constexpr SummaryPreset kSummaryPresets[] = {
    // Tiers follow the same context windows as the session budget presets: each
    // pool grows with the window so a large context is not spent on a
    // postcard-sized memory. The recent-message pool is half the session pool:
    // the per-session summaries already carry the shape of a conversation, so
    // the verbatim tail only needs to cover the newest stretch.
    {"Instant", 1'000, 2'000, 1'000},        //  32k window
    {"Very Light", 2'000, 4'000, 2'000},     //  64k window
    {"Light", 4'000, 8'000, 4'000},          // 128k window
    {"Balanced", 8'000, 16'000, 8'000},      // 200k window
    {"Heavy", 12'000, 32'000, 16'000},       // 400k window
    {"Very Heavy", 16'000, 48'000, 24'000},  //   1M window
};

// The reasoning levels offered for the chat and memory selectors. They are the
// same fixed ladder for every model; a stored level absent from it is still
// appended by rebuild_reasoning so it stays correctable.
const char* kReasoningLevels[] = {"none", "low", "medium", "high"};

} // namespace

AssistantForm::AssistantForm(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("AssistantFormWidget"));

  auto* content = new QVBoxLayout(this);
  content->setContentsMargins(0, 0, 12, 0);
  content->setSpacing(12);

  content->addWidget(section_label(this, trs("Assistant")));
  {
    auto* form = form_layout(this);

    name_ = new QLineEdit(this);
    name_->setPlaceholderText(trs("Assistant name"));

    model_ = new QLineEdit(this);
    model_->setPlaceholderText(trs("Model id, e.g. gpt-4o"));

    reasoning_ = new ComboBox(this);
    reasoning_->setEditable(true);

    protocol_ = new ComboBox(this);
    protocol_->addItem(
        trs("Responses API"), static_cast<int>(felis::bell::api_protocol::responses));
    protocol_->addItem(
        trs("Chat Completions"), static_cast<int>(felis::bell::api_protocol::chat_completions));

    form->addRow(field_label(this, trs("Name")), name_);
    form->addRow(field_label(this, trs("Model")), model_);
    form->addRow(field_label(this, trs("Reasoning")), reasoning_);
    form->addRow(field_label(this, trs("API protocol")), protocol_);
    content->addLayout(form);

    connect(model_, &QLineEdit::textChanged, this, [this](const QString&) {
      rebuild_reasoning(reasoning_, reasoning_->currentText());
    });
  }

  content->addWidget(h_separator(this));

  content->addWidget(section_label(this, trs("Connection")));
  {
    auto* form = form_layout(this);

    api_url_ = new QLineEdit(this);
    api_url_->setPlaceholderText(trs("https://api.example.com/v1"));
    api_url_->installEventFilter(this);

    token_ = new QLineEdit(this);
    token_->setPlaceholderText(trs("Authentication token"));
    token_->setEchoMode(QLineEdit::Password);

    proxy_ = new QLineEdit(this);
    proxy_->setPlaceholderText(trs("Optional"));

    search_endpoint_ = new QLineEdit(this);
    search_endpoint_->setPlaceholderText(QStringLiteral("http://127.0.0.1:8873"));

    turn_timeout_ = make_spin(this, 180);
    turn_timeout_->setRange(30, 3600);
    turn_timeout_->setSingleStep(30);
    turn_timeout_->setSuffix(QStringLiteral(" s"));

    summary_timeout_ = make_spin(this, 420);
    summary_timeout_->setRange(30, 7200);
    summary_timeout_->setSingleStep(60);
    summary_timeout_->setSuffix(QStringLiteral(" s"));

    form->addRow(field_label(this, trs("API URL")), api_url_);
    form->addRow(field_label(this, trs("Token")), token_);
    form->addRow(field_label(this, trs("Proxy")), proxy_);
    form->addRow(field_label(this, trs("Search endpoint")), search_endpoint_);
    form->addRow(field_label(this, trs("Turn timeout")), turn_timeout_);
    form->addRow(field_label(this, trs("Memory update timeout")), summary_timeout_);
    content->addLayout(form);
  }

  content->addWidget(h_separator(this));

  content->addWidget(section_label(this, trs("Custom instructions")));
  instructions_ = new PlainTextEdit(this);
  instructions_->setPlaceholderText(trs("System prompt for this assistant"));
  instructions_->setMinimumHeight(96);
  content->addWidget(instructions_);

  content->addWidget(h_separator(this));

  content->addWidget(section_label(this, trs("Memory preset")));

  preset_ = new ComboBox(this);
  preset_->addItem(trs("Apply a preset..."));
  for (const SummaryPreset& summary_preset: kSummaryPresets) {
    preset_->addItem(trs(summary_preset.name));
  }
  content->addWidget(preset_);

  content->addWidget(h_separator(this));

  content->addWidget(section_label(this, trs("Budgets")));
  {
    auto* form = form_layout(this);
    summary_token_budget_ = make_spin(this, 0);
    summary_session_budget_ = make_spin(this, 0);
    summary_message_pool_ = make_spin(this, 0);

    form->addRow(field_label(this, trs("Memory budget")), summary_token_budget_);
    // The two pools a memory update reads: each session's newest summary, and the
    // newest messages across all sessions.
    form->addRow(field_label(this, trs("Session pool")), summary_session_budget_);
    form->addRow(field_label(this, trs("Recent messages pool")), summary_message_pool_);
    content->addLayout(form);
  }

  connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
    if (index <= 0) {
      return;
    }

    const SummaryPreset& summary_preset = kSummaryPresets[index - 1];
    summary_token_budget_->setValue(summary_preset.memory_budget);
    summary_session_budget_->setValue(summary_preset.session_pool);
    summary_message_pool_->setValue(summary_preset.message_pool);
  });

  content->addStretch(1);
}

void AssistantForm::set_assistant(const felis::bell::assistant& assistant) {
  name_->setText(to_qstring(assistant.name_));
  model_->setText(to_qstring(assistant.model_));

  rebuild_reasoning(reasoning_, to_qstring(assistant.reasoning_effort_));

  const int protocol_index = protocol_->findData(static_cast<int>(assistant.protocol_));
  protocol_->setCurrentIndex(protocol_index >= 0 ? protocol_index : 0);

  api_url_->setText(to_qstring(assistant.api_url_));
  token_->setText(to_qstring(assistant.authentication_token_));
  proxy_->setText(
      assistant.proxy_.has_value() ? to_qstring(*assistant.proxy_) : QString());
  search_endpoint_->setText(to_qstring(assistant.search_endpoint_));
  turn_timeout_->setValue(static_cast<int>(assistant.turn_timeout_seconds_));
  summary_timeout_->setValue(static_cast<int>(assistant.summary_timeout_seconds_));
  instructions_->setPlainText(to_qstring(assistant.custom_instructions_));

  summary_token_budget_->setValue(static_cast<int>(assistant.memory_budget_));
  summary_session_budget_->setValue(
      static_cast<int>(assistant.memory_budget_for_session_compact_));
  summary_message_pool_->setValue(
      static_cast<int>(assistant.memory_budget_across_messages_));
}

void AssistantForm::write_into(felis::bell::assistant& assistant) const {
  assistant.name_ = name_->text().trimmed().toStdString();
  if (assistant.name_.empty()) {
    assistant.name_ = "Untitled assistant";
  }

  assistant.authentication_token_ = token_->text().trimmed().toStdString();
  assistant.model_ = model_->text().trimmed().toStdString();
  assistant.api_url_ = api_url_->text().trimmed().toStdString();

  assistant.protocol_ =
      static_cast<felis::bell::api_protocol>(protocol_->currentData().toInt());
  assistant.reasoning_effort_ = reasoning_->currentText().toStdString();

  const QString proxy = proxy_->text().trimmed();
  assistant.proxy_ =
      proxy.isEmpty() ? std::nullopt : std::optional<std::string>(proxy.toStdString());

  assistant.search_endpoint_ = search_endpoint_->text().trimmed().toStdString();
  assistant.turn_timeout_seconds_ = static_cast<std::size_t>(turn_timeout_->value());
  assistant.summary_timeout_seconds_ = static_cast<std::size_t>(summary_timeout_->value());
  assistant.custom_instructions_ = instructions_->toPlainText().toStdString();

  assistant.memory_budget_ = static_cast<std::size_t>(summary_token_budget_->value());
  assistant.memory_budget_for_session_compact_ =
      static_cast<std::size_t>(summary_session_budget_->value());
  assistant.memory_budget_across_messages_ =
      static_cast<std::size_t>(summary_message_pool_->value());
}

bool AssistantForm::validate_base_url(QString& error) const {
  const QString url = api_url_->text().trimmed();

  if (url.isEmpty() || (!url.startsWith(QStringLiteral("http://")) &&
                        !url.startsWith(QStringLiteral("https://")))) {
    error = trs("The API URL must be an http:// or https:// base, for example "
                "https://api.example.com/v1.");
    return false;
  }

  return true;
}

void AssistantForm::rebuild_reasoning(QComboBox* combo, const QString& preferred) {
  if (combo == nullptr) {
    return;
  }

  const QString wanted = preferred.trimmed();
  const QString target = wanted.isEmpty() ? QStringLiteral("medium") : wanted;

  const QSignalBlocker blocker(combo);
  combo->clear();

  int selected = 0;
  bool present = false;
  for (const char* level: kReasoningLevels) {
    const QString text = QString::fromLatin1(level);
    combo->addItem(text);
    if (QString::compare(text, target, Qt::CaseInsensitive) == 0) {
      selected = combo->count() - 1;
      present = true;
    }
  }

  // Keep a stored level visible even when it is not on the ladder.
  if (!present && !target.isEmpty()) {
    combo->addItem(target);
    selected = combo->count() - 1;
  }

  combo->setCurrentIndex(selected);
}

bool AssistantForm::eventFilter(QObject* watched, QEvent* event) {
  if (watched == api_url_ && event->type() == QEvent::FocusOut) {
    const QString text = api_url_->text().trimmed();
    if (!text.isEmpty() && !text.contains(QStringLiteral("://"))) {
      api_url_->setText(QStringLiteral("https://") + text);
    }
  }

  return QWidget::eventFilter(watched, event);
}

} // namespace ui
