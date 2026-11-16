#include "support/LogFormat.hpp"

#include <QTime>

#include "support/i18n.hpp"
#include "ui/theme.hpp"

namespace ui::log_format {

QString level_name(felis::log::message_type type) {
  switch (type) {
    case felis::log::message_type::DEBUG:
      return trs("Debug");
    case felis::log::message_type::ERROR:
      return trs("Error");
    case felis::log::message_type::INFO:
    default:
      return trs("Info");
  }
}

QColor level_colour(felis::log::message_type type) {
  const ThemeColors& colours = theme().colors;

  switch (type) {
    case felis::log::message_type::DEBUG:
      return colours.text_muted;
    case felis::log::message_type::ERROR:
      return colours.danger;
    case felis::log::message_type::INFO:
    default:
      return colours.text;
  }
}

QString current_timestamp() {
  return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

QString preview(const QString& body, int limit) {
  QString line = body;

  // Keep only the first line: a stack trace or a formatted body would otherwise
  // stretch the row's height and hide the entries around it.
  const qsizetype newline = line.indexOf(QLatin1Char('\n'));

  if (newline >= 0) {
    line.truncate(newline);
  }

  line.replace(QLatin1Char('\t'), QLatin1Char(' '));
  line = line.trimmed();

  if (line.size() > limit) {
    line.truncate(limit);
    line += QStringLiteral("…");
  }

  return line;
}

} // namespace ui::log_format
