#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QCheckBox;
class QListWidget;
class QPushButton;

namespace lnb {
struct Settings;
}

namespace felis::bell {
class store;
}

namespace ui {

// The startup assistant picker: choose an existing assistant, or create a new
// one, before the main window opens. It works against the assistant store only,
// because the client (which owns one assistant for the process) is constructed
// after the picker accepts.
class AssistantPicker : public QDialog {
public:
  explicit AssistantPicker(
      felis::bell::store& store, lnb::Settings& settings, QWidget* parent = nullptr
  );

private:
  void refresh();
  void update_actions();
  void open_selected();
  void edit_selected();
  void delete_selected();
  void create_new();

  [[nodiscard]] QString selected_uuid() const;
  void select_uuid(const QString& uuid);

  felis::bell::store& store_;
  lnb::Settings& settings_;

  QListWidget* list_ = nullptr;
  QCheckBox* auto_login_ = nullptr;
  QPushButton* edit_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
};

} // namespace ui
