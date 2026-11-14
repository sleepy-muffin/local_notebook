#pragma once

#include <QWidget>

#include "store.hpp"

class QComboBox;
class QEvent;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace ui {
class PlainTextEdit;

// A reusable editor for an assistant's configuration: identity, connection
// details, custom instructions and the assistant-level memory budgets. It is
// embedded both by the startup picker's create/edit dialog and by the Assistant
// page of the settings window, so the two never drift apart.
class AssistantForm : public QWidget {
public:
  explicit AssistantForm(QWidget* parent = nullptr);

  void set_assistant(const felis::bell::assistant& assistant);
  void write_into(felis::bell::assistant& assistant) const;

  // True when the API URL is usable (an http/https base). On false, `error`
  // carries a message. Callers must check this before writing the assistant.
  [[nodiscard]] bool validate_base_url(QString& error) const;

private:
  void rebuild_reasoning(QComboBox* combo, const QString& preferred);

  bool eventFilter(QObject* watched, QEvent* event) override;

  QLineEdit* name_ = nullptr;
  QLineEdit* model_ = nullptr;
  QLineEdit* api_url_ = nullptr;
  QLineEdit* token_ = nullptr;
  QLineEdit* proxy_ = nullptr;
  QLineEdit* search_endpoint_ = nullptr;
  QSpinBox* turn_timeout_ = nullptr;
  QSpinBox* summary_timeout_ = nullptr;
  PlainTextEdit* instructions_ = nullptr;
  QComboBox* protocol_ = nullptr;
  QComboBox* reasoning_ = nullptr;
  QComboBox* preset_ = nullptr;
  QSpinBox* summary_token_budget_ = nullptr;
  QSpinBox* summary_trigger_ = nullptr;
  QSpinBox* summary_session_budget_ = nullptr;
};

} // namespace ui
