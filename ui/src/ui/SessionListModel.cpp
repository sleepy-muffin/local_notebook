#include "ui/SessionListModel.hpp"

#include "support/i18n.hpp"
#include "ui/TimeFormat.hpp"

namespace ui {

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int SessionListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(sessions_.size());
}

QVariant SessionListModel::data(const QModelIndex& index, int role) const {
  const felis::bell::shadowed_session* session = session_at(index.row());

  if (session == nullptr) {
    return {};
  }

  switch (role) {
    case Qt::DisplayRole:
    case TitleRole: {
      const QString title = QString::fromStdString(session->title_);
      return title.isEmpty() ? trs("New Session") : title;
    }
    case IdRole:
      return session->id_;
    case TimeRole:
      return format_message_time(last_time_at(index.row()));
    case PinnedRole:
      return session->pinned_;
    default:
      return {};
  }
}

void SessionListModel::set_sessions(
    std::vector<felis::bell::shadowed_session> sessions,
    std::vector<std::string> last_times
) {
  beginResetModel();
  sessions_ = std::move(sessions);
  last_times_ = std::move(last_times);
  // Keep the two vectors parallel even if a caller passes a short list.
  last_times_.resize(sessions_.size());
  endResetModel();
}

const felis::bell::shadowed_session* SessionListModel::session_at(int row) const {
  if (row < 0 || row >= static_cast<int>(sessions_.size())) {
    return nullptr;
  }

  return &sessions_[static_cast<std::size_t>(row)];
}

std::uint32_t SessionListModel::session_id_at(int row) const {
  const felis::bell::shadowed_session* session = session_at(row);
  return session != nullptr ? session->id_ : 0;
}

const std::string& SessionListModel::last_time_at(int row) const {
  static const std::string empty;

  if (row < 0 || row >= static_cast<int>(last_times_.size())) {
    return empty;
  }

  return last_times_[static_cast<std::size_t>(row)];
}

bool SessionListModel::pinned_at(int row) const {
  const felis::bell::shadowed_session* session = session_at(row);
  return session != nullptr && session->pinned_;
}

int SessionListModel::row_for(std::uint32_t session_id) const {
  for (std::size_t i = 0; i < sessions_.size(); ++i) {
    if (sessions_[i].id_ == session_id) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

} // namespace ui
