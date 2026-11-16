#pragma once

// Wheel-safe spin boxes, combo boxes and font selectors.
//
// A plain QSpinBox, QComboBox or QFontComboBox changes its value when the mouse
// wheel passes over it, which is a hazard inside a scrolling form: the pointer
// crosses the field while the reader scrolls and the value silently changes.
// These subclasses ignore the wheel unconditionally.
//
// Ignoring (rather than consuming) the event lets Qt forward it to the enclosing
// scroll area, so the page still scrolls while the value stays put.
//
// They are drop-in replacements: use them wherever a QSpinBox, QDoubleSpinBox,
// QComboBox or QFontComboBox would otherwise be created. The combo boxes also cap
// their own minimum width (eliding a long current item instead), so no single
// field can force a form wider than its viewport.

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QSpinBox>
#include <QWheelEvent>

#include <algorithm>

namespace ui {

namespace detail {

  // A spin box's preferred width follows its widest possible value, which for the
  // token budgets is "1,000,000,000" — wide enough to push a form past its viewport
  // on platforms with large font metrics. Cap the *minimum* width at a
  // representative number instead: the field still grows to show the full value
  // whenever the form has room, but it can also shrink (eliding the value) rather
  // than widening the whole page.
  inline QSize capped_spin_minimum(const QWidget *widget, const QSize &natural) {
    const int width = widget->fontMetrics().horizontalAdvance(QStringLiteral("000,000")) + 32;
    QSize size = natural;
    size.setWidth(std::min(size.width(), width));
    return size;
  }

  // The framed stylesheets reserve this many pixels at a combo's right edge for
  // the chevron; the painting below is centred in that column.
  constexpr int kComboChevronColumn = 20;

  inline QColor combo_chevron_colour(const QWidget *widget) {
    const QPalette::ColorGroup group =
        widget->isEnabled() ? QPalette::Active : QPalette::Disabled;

    return widget->palette().color(group, QPalette::WindowText);
  }

  // Qt's stylesheet engine takes the drop-down subcontrol over as soon as a
  // `QComboBox::drop-down` rule exists, and from then on it never asks the base
  // style for the arrow: it only ever draws one when the sheet itself supplies a
  // `::down-arrow` image. Because the themed drop-down is deliberately just a
  // transparent column, the chevron has to be painted here instead.
  inline void paint_combo_chevron(const QWidget *widget, QPainter &painter, bool open) {
    const QColor colour = combo_chevron_colour(widget);

    if (!colour.isValid()) {
      return;
    }

    constexpr qreal kWidth = 7.0;
    constexpr qreal kHeight = 3.6;

    const qreal right = widget->width() - 1.0;
    const QRectF area(
        right - kComboChevronColumn + (kComboChevronColumn - kWidth) / 2.0,
        widget->height() / 2.0 - kHeight / 2.0, kWidth, kHeight
    );

    const QPointF tip(area.center().x(), open ? area.top() : area.bottom());
    const QPointF left(area.left(), open ? area.bottom() : area.top());
    const QPointF end(area.right(), open ? area.bottom() : area.top());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colour, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF({left, tip, end}));
    painter.restore();
  }

} // namespace detail

class SpinBox : public QSpinBox {
public:
  using QSpinBox::QSpinBox;

  explicit SpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {
    setButtonSymbols(QAbstractSpinBox::NoButtons);
  }

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }

  QSize minimumSizeHint() const override {
    return detail::capped_spin_minimum(this, QSpinBox::sizeHint());
  }
};

class DoubleSpinBox : public QDoubleSpinBox {
public:
  using QDoubleSpinBox::QDoubleSpinBox;

  explicit DoubleSpinBox(QWidget *parent = nullptr) : QDoubleSpinBox(parent) {
    setButtonSymbols(QAbstractSpinBox::NoButtons);
  }

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// A wheel-safe combo box whose minimum width is bounded: the size hint follows a
// modest contents length rather than the longest item, so a long theme or preset
// name (or wide platform font metrics) cannot push a form past its viewport.
class ComboBox : public QComboBox {
public:
  explicit ComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(6);
  }

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }

  void paintEvent(QPaintEvent *event) override {
    QComboBox::paintEvent(event);

    QPainter painter(this);
    detail::paint_combo_chevron(this, painter, view()->isVisible());
  }

  void showPopup() override {
    QComboBox::showPopup();
    update();
  }

  void hidePopup() override {
    QComboBox::hidePopup();
    update();
  }
};

class FontComboBox : public QFontComboBox {
public:
  explicit FontComboBox(QWidget *parent = nullptr) : QFontComboBox(parent) {
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(6);
  }

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }

  void paintEvent(QPaintEvent *event) override {
    QFontComboBox::paintEvent(event);

    QPainter painter(this);
    detail::paint_combo_chevron(this, painter, view()->isVisible());
  }

  void showPopup() override {
    QFontComboBox::showPopup();
    update();
  }

  void hidePopup() override {
    QFontComboBox::hidePopup();
    update();
  }
};

class PlainTextEdit : public QPlainTextEdit {
public:
  using QPlainTextEdit::QPlainTextEdit;

protected:
  void wheelEvent(QWheelEvent *event) override {
    if (!hasFocus()) {
      event->ignore();
      return;
    }

    QPlainTextEdit::wheelEvent(event);
  }
};

} // namespace ui
