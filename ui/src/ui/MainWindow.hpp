#pragma once

#include <QWidget>

#include <cstdint>
#include <unordered_map>

#include <felis/async.hpp>

#include "ui/BackgroundWidget.hpp"

class QEvent;
class QLabel;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace lnb {
struct Settings;
}

namespace felis::bell {
class client;
}

namespace ui {

class ConversationView;
class SessionItemDelegate;
class SessionListModel;
class SettingsWindow;

// The application shell: one full-width title bar across the top, a sidebar of
// sessions on the left and the transcript on the right. It owns the lazily
// created ConversationViews (one per open session) and drives their poll loop,
// so a generation keeps running while its session is off screen.
class MainWindow : public BackgroundWidget {
public:
  explicit MainWindow(
      felis::bell::client& api, lnb::Settings& settings,
      felis::async::multi_executor<void()>& executor, QWidget* parent = nullptr
  );
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent* event) override;

  // Swallows the button-held moves the session lists would otherwise use to
  // slide their current row while the pointer is dragged across rows.
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void build_ui();
  QWidget* build_title_bar();
  QWidget* build_sidebar();
  QWidget* build_right_pane();
  QWidget* build_status_bar();

  QListView* build_session_list(
      QWidget* parent, const QString& object_name, SessionListModel*& model,
      SessionItemDelegate*& delegate
  );

  void apply_window_style();
  void refresh_sessions();
  // Moves the sidebar highlight onto the session that is actually displayed.
  void sync_selection_to_current();
  void open_conversation(std::uint32_t session_id);
  ConversationView* ensure_conversation_view(std::uint32_t session_id);

  void new_session();
  void open_settings();

  void pin_session(std::uint32_t session_id);
  void edit_session(std::uint32_t session_id);
  void delete_session(std::uint32_t session_id);

  void poll_conversations();

  void toggle_sidebar();
  void refresh_sidebar_toggle();

  void update_header();
  void update_current_block();

  felis::bell::client& api_;
  lnb::Settings& settings_;
  felis::async::multi_executor<void()>& executor_;

  QWidget* sidebar_ = nullptr;
  QWidget* sidebar_separator_ = nullptr;
  bool sidebar_visible_ = true;

  // Pinned sessions get their own band above the regular list.
  QWidget* pinned_band_ = nullptr;
  QListView* pinned_view_ = nullptr;
  SessionListModel* pinned_model_ = nullptr;
  SessionItemDelegate* pinned_delegate_ = nullptr;

  QListView* sessions_view_ = nullptr;
  SessionListModel* sessions_model_ = nullptr;
  SessionItemDelegate* sessions_delegate_ = nullptr;

  QPushButton* new_session_button_ = nullptr;

  QLabel* current_name_ = nullptr;
  QLabel* current_model_ = nullptr;
  QToolButton* current_settings_ = nullptr;

  QLabel* brand_ = nullptr;
  QLabel* category_ = nullptr;

  QLabel* header_subtitle_ = nullptr;
  QLabel* header_time_ = nullptr;

  QStackedWidget* workspace_ = nullptr;
  QLabel* empty_hint_ = nullptr;

  QTimer* poll_timer_ = nullptr;
  SettingsWindow* settings_window_ = nullptr;

  QToolButton* sidebar_toggle_ = nullptr;

  std::uint32_t current_session_ = 0;
  std::unordered_map<std::uint32_t, ConversationView*> conversations_;
};

} // namespace ui
