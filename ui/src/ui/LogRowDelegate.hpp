#pragma once

// Custom painting for one row of the log inspector's list. A default
// QListWidgetItem can only carry one text colour, which would flatten the row's
// three parts (time, level, message) into the same tint. This delegate paints
// them separately: the timestamp recedes, the level carries its severity colour,
// and the message stays ordinary text.

#include <QStyledItemDelegate>

namespace ui::log_roles {

// Time of the record, formatted "HH:mm:ss".
inline constexpr int time = Qt::UserRole + 1;
// Translated level name, e.g. "Error".
inline constexpr int level = Qt::UserRole + 2;
// The single-line preview shown after the level.
inline constexpr int preview = Qt::UserRole + 3;
// The raw felis::log::message_type, so the delegate can colour the level.
inline constexpr int severity = Qt::UserRole + 4;
// True for a rendered LLM request dump, which is labelled and tinted separately.
inline constexpr int payload = Qt::UserRole + 5;

} // namespace ui::log_roles

namespace ui {

class LogRowDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(
      QPainter* painter, const QStyleOptionViewItem& option,
      const QModelIndex& index
  ) const override;

  QSize sizeHint(
      const QStyleOptionViewItem& option, const QModelIndex& index
  ) const override;
};

} // namespace ui
