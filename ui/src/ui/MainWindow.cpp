#include "ui/MainWindow.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "client.hpp"
#include "settings/settings.hpp"
#include "support/i18n.hpp"
#include "ui/ConversationView.hpp"
#include "ui/ScrollBar.hpp"
#include "ui/SessionEditor.hpp"
#include "ui/SessionItemDelegate.hpp"
#include "ui/SessionListModel.hpp"
#include "ui/SettingsWindow.hpp"
#include "ui/theme.hpp"

namespace ui {

namespace {

constexpr int kSessionListLimit = 500;
constexpr int kSidebarPadding = 12;
constexpr int kNewSessionPadding = 4;
constexpr int kBandHeight = 38;
constexpr int kStatusBarHeight = 26;

QString from_utf8(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString rgba(const QColor& color) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alphaF());
}

// Rebuilds a session row with a new pinned flag. `session` has no setter, so a
// value-level edit re-constructs it from its own getters (all of which are set).
felis::bell::session with_pin(const felis::bell::session& source, bool pinned) {
  using felis::bell::size_type;
  return felis::bell::session(
      size_type(source.id()), source.title(), source.custom_instructions(),
      source.reasoning_effort(), source.auto_compact_reasoning_effort(),
      source.shared_directories(), source.kind(), pinned,
      source.tools(), source.auto_compact_threshold(), source.auto_compact_preserve(),
      source.auto_compact_budget(), source.response_budget()
  );
}

} // namespace

MainWindow::MainWindow(
    felis::bell::client& api, lnb::Settings& settings,
    felis::async::multi_executor<void()>& executor, QWidget* parent
)
    : BackgroundWidget(parent, /*use_background_image=*/true),
      api_(api),
      settings_(settings),
      executor_(executor) {
  setWindowTitle(QStringLiteral("Local Notebook"));
  resize(1080, 720);
  setMinimumSize(720, 480);

  if (settings_.geometry.has_value()) {
    restoreGeometry(QByteArray::fromBase64(
        QByteArray::fromStdString(*settings_.geometry)
    ));
  }

  build_ui();
  refresh_sessions();

  const std::vector<felis::bell::shadowed_session> sessions = api_.sessions(kSessionListLimit);
  if (!sessions.empty()) {
    open_conversation(sessions.front().id_);
  } else {
    update_header();
  }

  poll_timer_ = new QTimer(this);
  connect(poll_timer_, &QTimer::timeout, this, [this]() { poll_conversations(); });
  poll_timer_->start(33);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
  if (poll_timer_ != nullptr) {
    poll_timer_->stop();
  }

  settings_.geometry = saveGeometry().toBase64().toStdString();
  lnb::save_settings(api_.get_store(), settings_);

  BackgroundWidget::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseMove) {
    auto* mouse = static_cast<QMouseEvent*>(event);

    if ((mouse->buttons() & Qt::LeftButton) != 0) {
      return true;
    }
  }

  return BackgroundWidget::eventFilter(watched, event);
}

void MainWindow::build_ui() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  root->addWidget(build_title_bar());

  auto* body = new QHBoxLayout();
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);

  sidebar_ = build_sidebar();
  body->addWidget(sidebar_);

  sidebar_separator_ = new QWidget(this);
  sidebar_separator_->setObjectName(QStringLiteral("SidebarSeparator"));
  sidebar_separator_->setAttribute(Qt::WA_StyledBackground, true);
  sidebar_separator_->setFixedWidth(1);
  body->addWidget(sidebar_separator_);

  body->addWidget(build_right_pane(), 1);

  root->addLayout(body, 1);
  root->addWidget(build_status_bar());

  apply_window_style();
}

QWidget* MainWindow::build_status_bar() {
  auto* bar = new QFrame(this);
  bar->setObjectName(QStringLiteral("StatusBar"));
  bar->setAttribute(Qt::WA_StyledBackground, true);
  bar->setFixedHeight(kStatusBarHeight);

  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(6, 0, 10, 0);
  layout->setSpacing(8);

  sidebar_toggle_ = new QToolButton(bar);
  sidebar_toggle_->setObjectName(QStringLiteral("SidebarToggle"));
  sidebar_toggle_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  sidebar_toggle_->setAutoRaise(true);
  sidebar_toggle_->setFocusPolicy(Qt::NoFocus);
  sidebar_toggle_->setCursor(Qt::PointingHandCursor);
  sidebar_toggle_->setFixedSize(20, 20);
  sidebar_toggle_->setIconSize(QSize(15, 15));
  sidebar_toggle_->setToolTip(trs("Toggle sidebar"));

  connect(sidebar_toggle_, &QToolButton::clicked, this, [this]() { toggle_sidebar(); });

  layout->addWidget(sidebar_toggle_);
  layout->addStretch(1);

  return bar;
}

QWidget* MainWindow::build_title_bar() {
  auto* bar = new QFrame(this);
  bar->setObjectName(QStringLiteral("TitleBar"));
  bar->setAttribute(Qt::WA_StyledBackground, true);
  bar->setFixedHeight(kBandHeight);

  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(kSidebarPadding, 0, 16, 0);
  layout->setSpacing(10);

  brand_ = new QLabel(QStringLiteral("Local Notebook"), bar);
  brand_->setObjectName(QStringLiteral("Brand"));

  header_subtitle_ = new QLabel(bar);
  header_subtitle_->setObjectName(QStringLiteral("HeaderSubtitle"));

  header_time_ = new QLabel(bar);
  header_time_->setObjectName(QStringLiteral("HeaderTime"));

  layout->addWidget(brand_);
  layout->addStretch(1);
  layout->addWidget(header_subtitle_);
  layout->addSpacing(12);
  layout->addWidget(header_time_);

  return bar;
}

void MainWindow::apply_window_style() {
  const ThemeColors& c = theme().colors;

  setStyleSheet(
      QStringLiteral(
          "QLabel#Brand { color: %1; }"
          "QLabel#SidebarCategory { color: %2; }"
          "QLabel#HeaderSubtitle, QLabel#HeaderTime { color: %2; }"
          "QLabel#CurrentName { color: %1; }"
          "QLabel#CurrentModel { color: %2; }"
          "QLabel#EmptyHint { color: %2; }"
          "QWidget#Sidebar { background: %4; }"
          "QWidget#SidebarSeparator { background: %3; }"
          "QFrame#TitleBar { background: %4; border: none;"
          "  border-bottom: 1px solid %3; }"
          "QFrame#StatusBar { background: %4; border: none;"
          "  border-top: 1px solid %3; }"
          "QToolButton#SidebarToggle { background: transparent;"
          "  border: none; border-radius: 4px; padding: 0 4px; color: %2; }"
          "QToolButton#SidebarToggle:hover { background: %5; }"
          "QFrame#CurrentUser { background: transparent; border: none;"
          "  border-top: 1px solid %3; }"
          "QListView#Sessions { border: none; outline: none; }"
          "QPushButton#NewSession { background: transparent; border: none; color: %1;"
          "  text-align: left; padding: 6px 12px; border-radius: 0; }"
          "QPushButton#NewSession:hover { background: %5; }"
          "QToolButton#CurrentSettings { background: transparent; border: none; padding: 0; }"
          "QToolButton#CurrentSettings:hover { background: %5; border-radius: 4px; }"
      )
          .arg(c.text.name(), c.text_muted.name(), c.border.name(), rgba(c.surface),
               rgba(row_hover_background()))
  );

  if (brand_ != nullptr) {
    QFont font = ui_font();
    font.setWeight(QFont::DemiBold);
    if (font.pointSizeF() > 1.0) {
      font.setPointSizeF(font.pointSizeF() + 1.0);
    }
    brand_->setFont(font);
  }

  if (category_ != nullptr) {
    QFont font = ui_font();
    if (font.pointSizeF() > 1.5) {
      font.setPointSizeF(font.pointSizeF() - 1.0);
    }
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    category_->setFont(font);
  }

  if (current_model_ != nullptr) {
    QFont font = ui_font();
    if (font.pointSizeF() > 1.5) {
      font.setPointSizeF(font.pointSizeF() - 1.0);
    }
    current_model_->setFont(font);
  }

  if (new_session_button_ != nullptr) {
    QIcon add = resource_icon(QStringLiteral("new-session"), c.text, c.text_muted);

    if (add.isNull()) {
      add = QIcon::fromTheme(QStringLiteral("list-add"));
    }
    if (add.isNull()) {
      add = style()->standardIcon(QStyle::SP_FileDialogNewFolder);
    }

    new_session_button_->setIcon(add);
  }

  if (current_settings_ != nullptr) {
    QIcon gear = resource_icon(QStringLiteral("settings"), c.text_muted, c.text_muted);

    if (gear.isNull()) {
      gear = QIcon::fromTheme(QStringLiteral("settings-configure"));
    }
    if (gear.isNull()) {
      gear = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    }

    current_settings_->setIcon(gear);
  }

  refresh_sidebar_toggle();
}

QListView* MainWindow::build_session_list(
    QWidget* parent, const QString& object_name, SessionListModel*& model,
    SessionItemDelegate*& delegate
) {
  auto* view = new QListView(parent);
  view->setObjectName(object_name);
  show_parent_background(view);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setFrameShape(QFrame::NoFrame);
  view->setFrameShadow(QFrame::Plain);
  view->setLineWidth(0);
  view->setMidLineWidth(0);
  view->setSpacing(0);
  view->setUniformItemSizes(true);
  view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  install_scroll_bars(view, ScrollBarTrack::Surface);
  view->setSelectionRectVisible(false);
  view->setMouseTracking(true);
  view->viewport()->setMouseTracking(true);
  view->viewport()->setAutoFillBackground(false);
  view->setMinimumWidth(0);
  view->viewport()->installEventFilter(this);

  model = new SessionListModel(view);
  view->setModel(model);

  delegate = new SessionItemDelegate(view);
  view->setItemDelegate(delegate);

  SessionListModel* const bound_model = model;
  SessionItemDelegate* const bound_delegate = delegate;

  bound_delegate->set_pin_handler([this, bound_model](const QModelIndex& index) {
    pin_session(bound_model->session_id_at(index.row()));
  });
  bound_delegate->set_edit_handler([this, bound_model](const QModelIndex& index) {
    edit_session(bound_model->session_id_at(index.row()));
  });
  bound_delegate->set_delete_handler([this, bound_model](const QModelIndex& index) {
    delete_session(bound_model->session_id_at(index.row()));
  });

  connect(
      view, &QListView::pressed, this,
      [bound_delegate](const QModelIndex&) { bound_delegate->clear_action_click(); }
  );

  // Keep at most one of the two lists highlighted. The highlight is the selection
  // fill, so clearing the other list's selection is enough; clearing the selection
  // leaves its current index alone, which does not emit currentChanged and so
  // cannot recurse back into this handler.
  QListView* const bound_view = view;
  connect(
      view->selectionModel(), &QItemSelectionModel::currentChanged, this,
      [this, bound_view](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) {
          return;
        }

        QListView* const other =
            bound_view == pinned_view_ ? sessions_view_ : pinned_view_;

        if (other != nullptr) {
          other->clearSelection();
        }
      }
  );

  connect(view, &QListView::clicked, this, [this, bound_delegate](const QModelIndex& index) {
    if (bound_delegate->take_action_click()) {
      return;
    }
    if (index.isValid()) {
      open_conversation(sessions_model_->session_id_at(index.row()));
    }
  });

  return view;
}

QWidget* MainWindow::build_sidebar() {
  const lnb::ThemeMetrics& m = metrics();

  auto* sidebar = new QWidget(this);
  sidebar->setObjectName(QStringLiteral("Sidebar"));
  sidebar->setAttribute(Qt::WA_StyledBackground, true);
  sidebar->setFixedWidth(m.sidebar_width);

  auto* layout = new QVBoxLayout(sidebar);
  layout->setContentsMargins(0, 6, 0, 0);
  layout->setSpacing(0);

  auto* new_session_row = new QWidget(sidebar);
  auto* new_session_layout = new QHBoxLayout(new_session_row);
  new_session_layout->setContentsMargins(0, 0, 0, 0);
  new_session_layout->setSpacing(0);

  new_session_button_ = new QPushButton(trs("New Session"), new_session_row);
  new_session_button_->setObjectName(QStringLiteral("NewSession"));
  new_session_button_->setCursor(Qt::PointingHandCursor);
  new_session_button_->setFlat(true);
  new_session_button_->setIconSize(QSize(14, 14));
  new_session_button_->setFixedHeight(kBandHeight - 2 * kNewSessionPadding);

  connect(new_session_button_, &QPushButton::clicked, this, [this]() { new_session(); });

  new_session_layout->addWidget(new_session_button_);
  layout->addWidget(new_session_row);

  // ---- PINNED category + list ----
  // Above SESSIONS, and hidden entirely while no session is pinned. The list
  // sizes to its rows, so the SESSIONS category slides up under the New Session
  // row whenever the band disappears.
  pinned_band_ = new QWidget(sidebar);
  auto* pinned_layout = new QVBoxLayout(pinned_band_);
  pinned_layout->setContentsMargins(0, 0, 0, 0);
  pinned_layout->setSpacing(0);

  {
    auto* pinned_category = new QLabel(trs("PINNED"), pinned_band_);
    pinned_category->setObjectName(QStringLiteral("SidebarCategory"));
    pinned_category->setContentsMargins(kSidebarPadding, 10, 0, 4);
    pinned_layout->addWidget(pinned_category);
  }

  pinned_view_ = build_session_list(
      pinned_band_, QStringLiteral("PinnedSessions"), pinned_model_, pinned_delegate_
  );
  pinned_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  pinned_layout->addWidget(pinned_view_);
  pinned_band_->setVisible(false);

  layout->addWidget(pinned_band_);

  category_ = new QLabel(trs("SESSIONS"), sidebar);
  category_->setObjectName(QStringLiteral("SidebarCategory"));
  category_->setContentsMargins(kSidebarPadding, 10, 0, 4);
  layout->addWidget(category_);

  sessions_view_ = build_session_list(
      sidebar, QStringLiteral("Sessions"), sessions_model_, sessions_delegate_
  );
  layout->addWidget(sessions_view_, 1);

  auto* current_user = new QFrame(sidebar);
  current_user->setObjectName(QStringLiteral("CurrentUser"));
  current_user->setAttribute(Qt::WA_StyledBackground, true);
  auto* current_layout = new QHBoxLayout(current_user);
  current_layout->setContentsMargins(kSidebarPadding, 8, 8, 8);
  current_layout->setSpacing(6);

  auto* current_text = new QVBoxLayout();
  current_text->setContentsMargins(0, 0, 0, 0);
  current_text->setSpacing(0);

  current_name_ = new QLabel(current_user);
  current_name_->setObjectName(QStringLiteral("CurrentName"));
  current_model_ = new QLabel(current_user);
  current_model_->setObjectName(QStringLiteral("CurrentModel"));

  current_text->addWidget(current_name_);
  current_text->addWidget(current_model_);
  current_layout->addLayout(current_text, 1);

  current_settings_ = new QToolButton(current_user);
  current_settings_->setObjectName(QStringLiteral("CurrentSettings"));
  current_settings_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  current_settings_->setAutoRaise(true);
  current_settings_->setFocusPolicy(Qt::NoFocus);
  current_settings_->setCursor(Qt::PointingHandCursor);
  current_settings_->setIconSize(QSize(16, 16));
  connect(current_settings_, &QToolButton::clicked, this, [this]() { open_settings(); });
  current_layout->addWidget(current_settings_);

  layout->addWidget(current_user);

  return sidebar;
}

QWidget* MainWindow::build_right_pane() {
  auto* pane = new QWidget(this);
  auto* layout = new QVBoxLayout(pane);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  workspace_ = new QStackedWidget(pane);

  empty_hint_ = new QLabel(trs("Select a session, or create a new one."), workspace_);
  empty_hint_->setObjectName(QStringLiteral("EmptyHint"));
  empty_hint_->setAlignment(Qt::AlignCenter);
  workspace_->addWidget(empty_hint_);

  layout->addWidget(workspace_, 1);

  return pane;
}

void MainWindow::refresh_sessions() {
  if (sessions_model_ == nullptr || pinned_model_ == nullptr) {
    return;
  }

  std::vector<felis::bell::shadowed_session> sessions = api_.sessions(kSessionListLimit);
  const std::unordered_map<std::uint32_t, std::string> last_times = api_.session_last_times();

  // Split into the two categories. sessions_by_recent() already returns pinned
  // sessions first and each group keeps its newest-message order, so no extra
  // sort is needed.
  std::vector<felis::bell::shadowed_session> pinned;
  std::vector<felis::bell::shadowed_session> rest;
  std::vector<std::string> pinned_times;
  std::vector<std::string> rest_times;

  for (const felis::bell::shadowed_session& session: sessions) {
    const auto it = last_times.find(session.id_);
    std::string time = it != last_times.end() ? it->second : std::string{};

    if (session.pinned_) {
      pinned.push_back(session);
      pinned_times.push_back(std::move(time));
    } else {
      rest.push_back(session);
      rest_times.push_back(std::move(time));
    }
  }

  // Only reset a model when its order, titles, times or pin states actually
  // changed, so a new message does not clear and rebuild the lists on every turn.
  const auto unchanged = [](
                             const SessionListModel* model,
                             const std::vector<felis::bell::shadowed_session>& rows,
                             const std::vector<std::string>& times
                         ) {
    if (static_cast<std::size_t>(model->rowCount()) != rows.size()) {
      return false;
    }

    for (int row = 0; row < model->rowCount(); ++row) {
      const felis::bell::shadowed_session* stored = model->session_at(row);
      const felis::bell::shadowed_session& wanted = rows[static_cast<std::size_t>(row)];

      if (stored == nullptr || stored->id_ != wanted.id_ || stored->title_ != wanted.title_ ||
          stored->pinned_ != wanted.pinned_ ||
          model->last_time_at(row) != times[static_cast<std::size_t>(row)]) {
        return false;
      }
    }

    return true;
  };

  if (!unchanged(pinned_model_, pinned, pinned_times)) {
    pinned_model_->set_sessions(std::move(pinned), std::move(pinned_times));
  }

  if (!unchanged(sessions_model_, rest, rest_times)) {
    sessions_model_->set_sessions(std::move(rest), std::move(rest_times));
  }

  // The pinned band sizes to its rows and disappears entirely when empty.
  int pinned_height = 0;

  for (int row = 0; row < pinned_model_->rowCount(); ++row) {
    pinned_height += std::max(0, pinned_view_->sizeHintForRow(row));
  }

  pinned_view_->setFixedHeight(pinned_height);
  pinned_band_->setVisible(pinned_model_->rowCount() > 0);

  // Re-select the current session (a model reset clears the selection). It lives
  // in exactly one of the two lists.
  const int pinned_row = pinned_model_->row_for(current_session_);

  if (pinned_row >= 0) {
    pinned_view_->setCurrentIndex(pinned_model_->index(pinned_row, 0));
    sessions_view_->clearSelection();
  } else {
    pinned_view_->clearSelection();

    const int row = sessions_model_->row_for(current_session_);
    if (row >= 0) {
      sessions_view_->setCurrentIndex(sessions_model_->index(row, 0));
    } else {
      sessions_view_->clearSelection();
    }
  }

  // Remove conversation views whose session no longer exists.
  for (auto it = conversations_.begin(); it != conversations_.end();) {
    if (!api_.session_exists(felis::bell::size_type(it->first))) {
      it->second->deleteLater();
      it = conversations_.erase(it);
    } else {
      ++it;
    }
  }

  update_current_block();
}

ConversationView* MainWindow::ensure_conversation_view(std::uint32_t session_id) {
  const auto existing = conversations_.find(session_id);
  if (existing != conversations_.end()) {
    return existing->second;
  }

  if (!api_.session_exists(felis::bell::size_type(session_id))) {
    return nullptr;
  }

  auto* view = new ConversationView(api_, settings_, session_id, executor_, workspace_);

  connect(view, &QWidget::destroyed, this, [this, session_id]() {
    conversations_.erase(session_id);
  });

  conversations_[session_id] = view;
  workspace_->addWidget(view);

  view->set_on_changed([this]() {
    refresh_sessions();
    update_header();
  });

  return view;
}

void MainWindow::open_conversation(std::uint32_t session_id) {
  ConversationView* view = ensure_conversation_view(session_id);

  if (view == nullptr) {
    return;
  }

  current_session_ = session_id;
  workspace_->setCurrentWidget(view);

  const int pinned_row = pinned_model_->row_for(session_id);
  if (pinned_row >= 0) {
    pinned_view_->setCurrentIndex(pinned_model_->index(pinned_row, 0));
    sessions_view_->clearSelection();
  } else {
    pinned_view_->clearSelection();

    const int row = sessions_model_->row_for(session_id);
    if (row >= 0) {
      sessions_view_->setCurrentIndex(sessions_model_->index(row, 0));
    }
  }

  update_header();
}

void MainWindow::new_session() {
  const felis::bell::size_type id =
      api_.create_session(felis::bell::shadowed_session(0, trs("New Session").toStdString()));
  refresh_sessions();
  open_conversation(id.get());
  edit_session(id.get());
}

void MainWindow::open_settings() {
  if (settings_window_ == nullptr) {
    settings_window_ = new SettingsWindow(api_, settings_, this);
    settings_window_->set_on_saved([this]() {
      apply_window_style();
      refresh_sessions();
      update_header();
      for (auto& entry: conversations_) {
        entry.second->mark_style_stale();
      }
    });
  }

  settings_window_->set_current_session(current_session_);
  settings_window_->show();
  settings_window_->raise();
  settings_window_->activateWindow();
}

void MainWindow::pin_session(std::uint32_t session_id) {
  const std::optional<felis::bell::session> stored =
      api_.load_session(felis::bell::size_type(session_id));

  if (!stored.has_value()) {
    return;
  }

  api_.save_session(with_pin(*stored, !stored->pinned()));
  refresh_sessions();
}

void MainWindow::edit_session(std::uint32_t session_id) {
  SessionEditor editor(api_, session_id, this);

  if (editor.exec() != QDialog::Accepted) {
    return;
  }

  refresh_sessions();
  update_header();

  const auto it = conversations_.find(editor.session_id());
  if (it != conversations_.end()) {
    it->second->reload();
  }
}

void MainWindow::delete_session(std::uint32_t session_id) {
  const QMessageBox::StandardButton answer = QMessageBox::warning(
      this, trs("Delete Session"),
      trs("Delete this session and its whole history? This cannot be undone."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (answer != QMessageBox::Yes) {
    return;
  }

  const auto it = conversations_.find(session_id);
  if (it != conversations_.end()) {
    it->second->deleteLater();
    conversations_.erase(it);
  }

  if (current_session_ == session_id) {
    current_session_ = 0;
    workspace_->setCurrentWidget(empty_hint_);
  }

  api_.delete_session(felis::bell::size_type(session_id));
  refresh_sessions();
  update_header();
}

void MainWindow::poll_conversations() {
  for (auto& entry: conversations_) {
    entry.second->poll();
  }
}

void MainWindow::toggle_sidebar() {
  sidebar_visible_ = !sidebar_visible_;
  sidebar_->setVisible(sidebar_visible_);
  sidebar_separator_->setVisible(sidebar_visible_);
  refresh_sidebar_toggle();
}

void MainWindow::refresh_sidebar_toggle() {
  if (sidebar_toggle_ == nullptr) {
    return;
  }

  const ThemeColors& c = theme().colors;
  const QColor colour = sidebar_visible_ ? c.accent : c.text_muted;
  QIcon icon = resource_icon(QStringLiteral("sidebar"), colour, colour);

  if (icon.isNull()) {
    icon = style()->standardIcon(QStyle::SP_FileDialogListView);
  }

  sidebar_toggle_->setIcon(icon);
}

void MainWindow::update_header() {
  if (current_session_ == 0) {
    header_subtitle_->clear();
    header_time_->clear();
    return;
  }

  const std::optional<felis::bell::session> session =
      api_.load_session(felis::bell::size_type(current_session_));

  if (session.has_value()) {
    header_subtitle_->setText(from_utf8(session->title()));
  } else {
    header_subtitle_->clear();
  }

  const std::optional<std::uint64_t> usage =
      api_.context_usage(felis::bell::size_type(current_session_));

  if (usage.has_value()) {
    header_time_->setText(trs("%1 tok").arg(static_cast<qulonglong>(*usage)));
  } else {
    header_time_->clear();
  }
}

void MainWindow::update_current_block() {
  if (current_name_ == nullptr || current_model_ == nullptr) {
    return;
  }

  const felis::bell::assistant assistant = api_.current_assistant();
  current_name_->setText(
      assistant.name_.empty() ? trs("Assistant") : from_utf8(assistant.name_));
  current_model_->setText(from_utf8(assistant.model_));
}

} // namespace ui
