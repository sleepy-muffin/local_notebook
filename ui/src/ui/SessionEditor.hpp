#pragma once

#include <QDialog>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace felis::bell {
class client;
}

namespace ui {
class PlainTextEdit;

// Edits one session: its title, custom instructions, shared directories, tool
// permissions and every session-bound budget. Pass a session_id of 0 to create a
// new session; the accepted id is exposed by session_id().
class SessionEditor : public QDialog {
public:
  SessionEditor(
      felis::bell::client& api, std::uint32_t session_id, QWidget* parent = nullptr
  );

  [[nodiscard]] std::uint32_t session_id() const { return session_id_; }

private:
  void load_session();
  void save_session();

  felis::bell::client& api_;

  // 0 while creating; the stored id once the session exists.
  std::uint32_t session_id_ = 0;

  QLineEdit* title_ = nullptr;
  QPlainTextEdit* instructions_ = nullptr;
  PlainTextEdit* shared_directories_ = nullptr;

  QCheckBox* online_tools_ = nullptr;
  QCheckBox* read_tools_ = nullptr;
  QCheckBox* write_tools_ = nullptr;
  QCheckBox* bash_tools_ = nullptr;

  QComboBox* reasoning_ = nullptr;
  QComboBox* summary_reasoning_ = nullptr;

  QComboBox* budget_preset_ = nullptr;
  QSpinBox* threshold_ = nullptr;
  QSpinBox* preserve_ = nullptr;
  QSpinBox* compact_budget_ = nullptr;
  QSpinBox* response_budget_ = nullptr;
};

} // namespace ui
