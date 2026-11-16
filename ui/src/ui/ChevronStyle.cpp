#include "ui/ChevronStyle.hpp"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// The arrow strokes are drawn at this width and inset; both are expressed as
// fractions of the small rect the style hands us, so the glyph stays crisp at
// any font size.
constexpr qreal kStrokeWidth = 1.4;
constexpr qreal kHorizontalInset = 0.22;
constexpr qreal kVerticalInset = 0.24;

// A disabled arrow keeps the palette's own disabled colour, which the theme
// already sets to the muted text colour.
QColor arrow_colour(const QStyleOption* option) {
  const QPalette::ColorGroup group = (option->state & QStyle::State_Enabled) != 0
      ? QPalette::Active
      : QPalette::Disabled;

  return option->palette.color(group, QPalette::WindowText);
}

// Paints (or, for the few rects too small to hold a chevron, declines to paint)
// one arrow. Returns false when the caller should fall back to the base style.
bool paint_chevron(
    QPainter* painter, const QStyleOption* option, bool points_down
) {
  const QRect rect = option->rect;
  if (rect.width() < 6 || rect.height() < 4) {
    return false;
  }

  const qreal inset_x = std::round(rect.width() * kHorizontalInset);
  const qreal inset_y = std::round(rect.height() * kVerticalInset);
  const QRectF area = QRectF(rect).adjusted(inset_x, inset_y, -inset_x, -inset_y);

  const QPointF tip(area.center().x(), points_down ? area.bottom() : area.top());
  const QPointF left(area.left(), points_down ? area.top() : area.bottom());
  const QPointF right(area.right(), points_down ? area.top() : area.bottom());

  const QColor colour = arrow_colour(option);
  if (!colour.isValid()) {
    return false;
  }

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setBrush(Qt::NoBrush);
  painter->setPen(
      QPen(colour, kStrokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
  );
  painter->drawPolyline(QPolygonF({left, tip, right}));
  painter->restore();

  return true;
}

class ChevronStyle : public QProxyStyle {
public:
  explicit ChevronStyle(QStyle* base) : QProxyStyle(base) {}

  void drawPrimitive(
      PrimitiveElement element, const QStyleOption* option, QPainter* painter,
      const QWidget* widget
  ) const override {
    if (painter != nullptr && option != nullptr) {
      switch (element) {
      case PE_IndicatorArrowDown:
      case PE_IndicatorSpinDown:
        if (paint_chevron(painter, option, true)) {
          return;
        }
        break;
      case PE_IndicatorArrowUp:
      case PE_IndicatorSpinUp:
        if (paint_chevron(painter, option, false)) {
          return;
        }
        break;
      default:
        break;
      }
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }
};

// A private copy of the platform style, so the proxy owns it outright and the
// application's previous style object is not deleted twice. The copy is made
// through the style factory from the style's own key, which is the same key the
// platform style was installed under.
QStyle* clone_platform_style(QStyle* current) {
  if (current == nullptr) {
    return nullptr;
  }

  const QString key = current->objectName();

  if (key.isEmpty()) {
    return nullptr;
  }

  return QStyleFactory::create(key);
}

} // namespace

void install_chevron_style(QApplication& app) {
  static bool installed = false;

  if (installed) {
    return;
  }

  if (dynamic_cast<ChevronStyle*>(app.style()) != nullptr) {
    installed = true;
    return;
  }

  if (QStyle* base = clone_platform_style(app.style())) {
    app.setStyle(new ChevronStyle(base));
    installed = true;
  }
}

} // namespace ui
