#include "ui/LogRowDelegate.hpp"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QStyle>

#include <felis/log.hpp>

#include "support/LogFormat.hpp"
#include "ui/theme.hpp"

namespace ui {

namespace log_row_style {

constexpr int kRowPadding = 5;
constexpr int kHorizontalPadding = 10;
constexpr int kRadius = 6;

} // namespace log_row_style

using namespace log_row_style;

void LogRowDelegate::paint(
    QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index
) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);

  const QRect row = option.rect.adjusted(0, 1, 0, -1);

  if ((option.state & QStyle::State_Selected) != 0) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(row_selected_background());
    painter->drawRoundedRect(row, kRadius, kRadius);
  } else if ((option.state & QStyle::State_MouseOver) != 0) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(row_hover_background());
    painter->drawRoundedRect(row, kRadius, kRadius);
  }

  const ThemeColors& colours = theme().colors;
  const QFont font = ui_font();
  const QFontMetrics metrics(font);

  painter->setFont(font);

  // A baseline that vertically centres the text inside the padded row.
  const int baseline = row.top() + kRowPadding + metrics.ascent();

  int x = row.left() + kHorizontalPadding;

  const QString time = index.data(log_roles::time).toString() + QStringLiteral("  ");
  painter->setPen(colours.text_muted);
  painter->drawText(QPoint(x, baseline), time);
  x += metrics.horizontalAdvance(time);

  const auto severity =
      static_cast<felis::log::message_type>(index.data(log_roles::severity).toInt());
  const QString level = index.data(log_roles::level).toString() + QStringLiteral("  ");
  const bool is_payload = index.data(log_roles::payload).toBool();
  painter->setPen(is_payload ? colours.accent : log_format::level_colour(severity));
  painter->drawText(QPoint(x, baseline), level);
  x += metrics.horizontalAdvance(level);

  painter->setPen(colours.text);

  const int available = row.right() - kHorizontalPadding - x;
  const QRect preview_rect(x, row.top(), available, row.height());
  const QString preview = index.data(log_roles::preview).toString();

  painter->drawText(
      preview_rect, Qt::AlignVCenter | Qt::AlignLeft,
      metrics.elidedText(preview, Qt::ElideRight, available)
  );

  painter->restore();
}

QSize LogRowDelegate::sizeHint(
    const QStyleOptionViewItem& option, const QModelIndex& index
) const {
  Q_UNUSED(index);

  const QFontMetrics metrics(ui_font());
  return QSize(option.rect.width(), metrics.height() + kRowPadding * 2);
}

} // namespace ui
