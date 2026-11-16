#pragma once

// Custom painting for one row of the assistant picker's list. The picker shows
// two pieces of information per assistant — its name and the model it runs — and
// a plain QListWidgetItem can only give both the same colour and size. This
// delegate sets the name in the ordinary text colour and the model beneath it in
// a smaller, muted line, the way the sidebar's session rows read.

#include <QSize>
#include <QStyledItemDelegate>

namespace ui::assistant_roles {

// The assistant's display name ("Untitled assistant" when it has none).
inline constexpr int name = Qt::UserRole + 1;
// The model identifier the assistant is configured with.
inline constexpr int model = Qt::UserRole + 2;
// The assistant's uuid, used to open, edit or delete the row.
inline constexpr int uuid = Qt::UserRole + 3;

} // namespace ui::assistant_roles

namespace ui {

class AssistantRowDelegate : public QStyledItemDelegate {
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
