#include "ui/TimeFormat.hpp"

#include <QDate>
#include <QDateTime>

namespace ui {

QString format_message_time(const std::string& iso) {
  if (iso.empty()) {
    return {};
  }

  const QDateTime parsed = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);

  if (!parsed.isValid()) {
    return QString::fromStdString(iso);
  }

  const QDateTime local = parsed.toLocalTime();
  const QDate today = QDate::currentDate();

  if (local.date() == today) {
    return local.toString(QStringLiteral("HH:mm"));
  }
  if (local.date().year() == today.year()) {
    return local.toString(QStringLiteral("MMM d"));
  }
  return local.toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace ui
