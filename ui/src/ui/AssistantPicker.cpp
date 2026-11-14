#include "ui/AssistantPicker.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QUuid>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "store.hpp"
#include "support/i18n.hpp"
#include "settings/settings.hpp"
#include "ui/AssistantForm.hpp"
#include "ui/dialog_style.hpp"

namespace ui {

using namespace dialog_style;

namespace {

constexpr int k_uuid_role = Qt::UserRole + 1;

// A small dialog wrapping AssistantForm, used by both "create" and "edit".
// Validates the base URL before accepting so a broken endpoint is never saved.
class AssistantFormDialog : public QDialog {
public:
  AssistantFormDialog(const felis::bell::assistant& initial, QWidget* parent)
      : QDialog(parent), initial_(initial) {
    setObjectName(QStringLiteral("AssistantFormDialog"));
    setWindowTitle(trs("Assistant"));
    resize(520, 640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    form_ = new AssistantForm(scroll);
    scroll->setWidget(form_);
    root->addWidget(scroll, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("Secondary"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });
    root->addWidget(buttons);

    setStyleSheet(dialog_stylesheet(QStringLiteral("AssistantFormDialog")));
    form_->set_assistant(initial_);
  }

  void accept() override {
    QString error;
    if (!form_->validate_base_url(error)) {
      form_->setToolTip(error);
      return;
    }
    result_ = initial_;
    form_->write_into(result_);
    QDialog::accept();
  }

  [[nodiscard]] const felis::bell::assistant& result() const { return result_; }

private:
  felis::bell::assistant initial_;
  felis::bell::assistant result_;
  AssistantForm* form_ = nullptr;
};

QString new_uuid() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

AssistantPicker::AssistantPicker(
    felis::bell::store& store, lnb::Settings& settings, QWidget* parent
)
    : QDialog(parent), store_(store), settings_(settings) {
  setObjectName(QStringLiteral("AssistantPicker"));
  setWindowTitle(trs("Local Notebook"));
  resize(520, 560);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 24, 24, 20);
  root->setSpacing(14);

  auto* title = new QLabel(trs("Choose an assistant"), this);
  title->setObjectName(QStringLiteral("PickerTitle"));
  apply_derived_font(title, 2.0, true);
  root->addWidget(title);

  auto* subtitle = new QLabel(trs("Each assistant is a separate notebook."), this);
  subtitle->setObjectName(QStringLiteral("PickerSubtitle"));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("AssistantList"));
  list_->setSpacing(2);
  root->addWidget(list_, 1);

  connect(list_, &QListWidget::itemSelectionChanged, this, [this]() { update_actions(); });
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
    open_selected();
  });

  auto_login_ = new QCheckBox(trs("Open this assistant automatically next time"), this);
  root->addWidget(auto_login_);

  auto* footer = new QHBoxLayout();
  footer->setSpacing(8);

  auto* new_button = new QPushButton(trs("New"), this);
  new_button->setObjectName(QStringLiteral("Secondary"));
  connect(new_button, &QPushButton::clicked, this, [this]() { create_new(); });

  edit_button_ = new QPushButton(trs("Edit"), this);
  edit_button_->setObjectName(QStringLiteral("Secondary"));
  connect(edit_button_, &QPushButton::clicked, this, [this]() { edit_selected(); });

  auto* delete_button = new QPushButton(trs("Delete"), this);
  delete_button->setObjectName(QStringLiteral("Secondary"));
  connect(delete_button, &QPushButton::clicked, this, [this]() { delete_selected(); });

  open_button_ = new QPushButton(trs("Open"), this);
  open_button_->setObjectName(QStringLiteral("Primary"));
  connect(open_button_, &QPushButton::clicked, this, [this]() { open_selected(); });

  footer->addWidget(new_button);
  footer->addWidget(edit_button_);
  footer->addWidget(delete_button);
  footer->addStretch(1);
  footer->addWidget(open_button_);
  root->addLayout(footer);

  setStyleSheet(dialog_stylesheet(QStringLiteral("AssistantPicker")));

  refresh();
}

void AssistantPicker::refresh() {
  list_->clear();

  const std::vector<std::string> uuids = store_.scan_assistants();

  for (const std::string& uuid: uuids) {
    const felis::bell::assistant assistant = store_.load_assistant(uuid);

    const QString name = assistant.name_.empty() ? trs("Untitled assistant")
                                                 : to_qstring(assistant.name_);
    const QString model = to_qstring(assistant.model_);

    auto* item = new QListWidgetItem(
        QStringLiteral("%1\n%2").arg(name, model), list_);
    item->setData(k_uuid_role, to_qstring(uuid));
  }

  auto_login_->setChecked(settings_.auto_login);

  const QString last = to_qstring(settings_.last_assistant);
  int target = -1;
  for (int row = 0; row < list_->count(); ++row) {
    if (list_->item(row)->data(k_uuid_role).toString() == last) {
      target = row;
      break;
    }
  }

  if (target < 0 && list_->count() > 0) {
    target = 0;
  }

  if (target >= 0) {
    list_->setCurrentRow(target);
  }

  update_actions();
}

void AssistantPicker::update_actions() {
  const bool has_selection = !selected_uuid().isEmpty();
  edit_button_->setEnabled(has_selection);
  open_button_->setEnabled(has_selection);
}

QString AssistantPicker::selected_uuid() const {
  const QListWidgetItem* item = list_->currentItem();
  return item != nullptr ? item->data(k_uuid_role).toString() : QString();
}

void AssistantPicker::select_uuid(const QString& uuid) {
  for (int row = 0; row < list_->count(); ++row) {
    if (list_->item(row)->data(k_uuid_role).toString() == uuid) {
      list_->setCurrentRow(row);
      return;
    }
  }
}

void AssistantPicker::open_selected() {
  const QString uuid = selected_uuid();

  if (uuid.isEmpty()) {
    return;
  }

  settings_.last_assistant = uuid.toStdString();
  settings_.auto_login = auto_login_->isChecked();
  accept();
}

void AssistantPicker::create_new() {
  felis::bell::assistant initial;
  initial.model_ = "deepseek-flash";

  AssistantFormDialog form(initial, this);

  if (form.exec() != QDialog::Accepted) {
    return;
  }

  const QString uuid = new_uuid();
  store_.save_assistant(uuid.toStdString(), form.result());
  settings_.last_assistant = uuid.toStdString();
  refresh();
  select_uuid(uuid);
}

void AssistantPicker::edit_selected() {
  const QString uuid = selected_uuid();

  if (uuid.isEmpty()) {
    return;
  }

  const felis::bell::assistant existing = store_.load_assistant(uuid.toStdString());
  AssistantFormDialog form(existing, this);

  if (form.exec() != QDialog::Accepted) {
    return;
  }

  store_.save_assistant(uuid.toStdString(), form.result());
  refresh();
  select_uuid(uuid);
}

void AssistantPicker::delete_selected() {
  const QString uuid = selected_uuid();

  if (uuid.isEmpty()) {
    return;
  }

  const felis::bell::assistant existing = store_.load_assistant(uuid.toStdString());
  const QString name =
      existing.name_.empty() ? trs("Untitled assistant") : to_qstring(existing.name_);

  const QMessageBox::StandardButton answer = QMessageBox::warning(
      this, trs("Delete assistant"),
      trs("Delete \"%1\" and its whole notebook? This cannot be undone.").arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (answer != QMessageBox::Yes) {
    return;
  }

  store_.delete_assistant(uuid.toStdString());
  refresh();
}

} // namespace ui
