#include "ui/AssistantRowDelegate.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

#include "ui/theme.hpp"

namespace ui {

namespace {

constexpr int kVerticalPadding = 9;
constexpr int kHorizontalPadding = 12;
constexpr int kRadius = 6;
// The row inset, so the rounded shading never touches the list edges.
constexpr int kRowInset = 2;

// The model line is one point smaller than the name, but never collapses when a
// tiny UI font is configured.
QFont model_font() {
  QFont font = ui_font();

  if (font.pointSizeF() > 2.5) {
    font.setPointSizeF(font.pointSizeF() - 1.0);
  }

  return font;
}

} // namespace

void AssistantRowDelegate::paint(
    QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index
) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);

  const QRect row = option.rect.adjusted(kRowInset, 1, -kRowInset, -1);

  if ((option.state & QStyle::State_Selected) != 0) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(row_selected_background());
    painter->drawRoundedRect(row, kRadius, kRadius);
  } else if ((option.state & QStyle::State_MouseOver) != 0) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(row_hover_background());
    painter->drawRoundedRect(row, kRadius, kRadius);
  }

  const QFont name_font = ui_font();
  const QFont secondary_font = model_font();
  const QFontMetrics name_metrics(name_font);
  const QFontMetrics secondary_metrics(secondary_font);

  const QString name = index.data(assistant_roles::name).toString();
  const QString model = index.data(assistant_roles::model).toString();

  const int text_width = row.width() - kHorizontalPadding * 2;
  const int text_height = name_metrics.height() + secondary_metrics.height();
  const int top = row.top() + (row.height() - text_height) / 2;

  painter->setFont(name_font);
  painter->setPen(theme().colors.text);
  painter->drawText(
      QRect(row.left() + kHorizontalPadding, top, text_width, name_metrics.height()),
      Qt::AlignVCenter | Qt::AlignLeft,
      name_metrics.elidedText(name, Qt::ElideRight, text_width)
  );

  painter->setFont(secondary_font);
  painter->setPen(theme().colors.text_muted);
  painter->drawText(
      QRect(
          row.left() + kHorizontalPadding, top + name_metrics.height(), text_width,
          secondary_metrics.height()
      ),
      Qt::AlignVCenter | Qt::AlignLeft,
      secondary_metrics.elidedText(model, Qt::ElideRight, text_width)
  );

  painter->restore();
}

QSize AssistantRowDelegate::sizeHint(
    const QStyleOptionViewItem& option, const QModelIndex& index
) const {
  Q_UNUSED(index);

  return QSize(
      option.rect.width(),
      QFontMetrics(ui_font()).height() + QFontMetrics(model_font()).height() +
          kVerticalPadding * 2
  );
}

} // namespace ui
