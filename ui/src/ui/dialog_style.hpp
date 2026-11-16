#pragma once

// Small shared helpers for the application's dialogs. They keep the dialogs'
// chrome — headings, field labels, hairlines, form spacing — identical, so every
// window reads the same way (design-1.md §12: chrome drawn consistently, QSS kept
// to small scoped pieces).

#include <QAbstractButton>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSizePolicy>
#include <QSpinBox>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <cstddef>
#include <string>
#include <string_view>

#include "ui/SpinBox.hpp"
#include "ui/theme.hpp"

namespace ui::dialog_style {

inline QString to_qstring(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

inline QString to_qstring(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

inline QString colour_hex(const QColor& colour) {
  return colour.name(QColor::HexRgb);
}

// The scoped stylesheet for one dialog. Scoping every rule by the dialog's object
// name keeps it from leaking into the rest of the application.
inline QString dialog_stylesheet(const QString& object_name) {
  const ThemeColors& c = theme().colors;

  const QString window = colour_hex(c.window);
  const QString surface = colour_hex(c.surface);
  const QString base = colour_hex(c.base);
  const QString text = colour_hex(c.text);
  const QString muted = colour_hex(c.text_muted);
  const QString border = colour_hex(c.border);
  const QString accent = colour_hex(c.accent);
  const QString accent_hover = colour_hex(c.accent_hover);
  const QString accent_active = colour_hex(c.accent_active);
  const QString accent_text = colour_hex(c.accent_text);

  QString sheet;

  sheet += QStringLiteral("QDialog#%1 { background: %2; }").arg(object_name, window);
  sheet += QStringLiteral("QLabel { background: transparent; }");
  sheet += QStringLiteral(
               "QLabel#PickerTitle, QLabel#FormTitle, QLabel#DialogTitle,"
               " QLabel#PreviewName, QLabel#PageTitle { color: %1; }")
               .arg(text);
  sheet += QStringLiteral(
               "QLabel#PickerSubtitle, QLabel#FormSubtitle, QLabel#DialogSubtitle,"
               " QLabel#PreviewModel, QLabel#PreviewHint, QLabel#FieldLabel,"
               " QLabel#SectionLabel, QLabel#PageSubtitle, QLabel#Muted { color: %1; }")
               .arg(muted);

  sheet += QStringLiteral("QWidget#Separator { background: %1; }").arg(border);
  sheet += QStringLiteral("QWidget#Hairline { background: %1; }").arg(border);

  sheet += QStringLiteral(
               "QListWidget#AssistantList, QListWidget#SettingsNav, QListWidget#SessionNav {"
               " border: none; outline: none; }");
  // The assistant picker's rows are painted by ui::AssistantRowDelegate, which
  // draws the same low-contrast hover/selection shading as the sidebar's session
  // list and sets the name and model in two different weights.
  // The settings window's left rail is the same kind of list. Without item rules
  // its entries collapse to the platform's minimal text rows, which reads as
  // tiny compared with the rest of the chrome; give them the shared row padding
  // and the low-contrast hover/selected shading the session list uses.
  sheet += QStringLiteral(
               "QListWidget#SettingsNav, QListWidget#SessionNav { padding: 6px; }");
  sheet += QStringLiteral(
               "QListWidget#SettingsNav::item, QListWidget#SessionNav::item {"
               "  padding: 8px 10px; border-radius: 6px; }");
  sheet += QStringLiteral(
               "QListWidget#SettingsNav::item:hover, QListWidget#SessionNav::item:hover {"
               "  background: %1; }")
               .arg(colour_hex(row_hover_background()));
  sheet += QStringLiteral(
               "QListWidget#SettingsNav::item:selected, QListWidget#SessionNav::item:selected {"
               "  background: %1; color: %2; }")
               .arg(colour_hex(row_selected_background()), text);
  // The scroll area keeps the dialog canvas (the window colour) in its frame
  // region without painting a background of its own: a `background: transparent`
  // here would be resolved into the scroll area's palette, which Breeze paints
  // opaque black around its scrollbars. See ui::show_parent_background().
  sheet += QStringLiteral(
               "QScrollArea { border: none; }"
               "QScrollArea > QWidget > QWidget { background: transparent; }");

  // Scrollbars carry only their handle. Breeze would otherwise fill the groove,
  // the add/sub-page areas and the scroll area corner from its own palette, which
  // reads as an opaque black strip inside a text area whose field is transparent.
  // The dialogs also replace their bars with ui::ScrollBar (which paints the
  // track itself), but this rule covers every bar the platform creates on its
  // own — popup views, spin boxes, and any area added later.
  sheet += QStringLiteral(
               "QScrollBar { background: transparent; border: none; }"
               "QScrollBar::groove:vertical, QScrollBar::groove:horizontal {"
               "  background: transparent; border: none; }"
               "QScrollBar::handle:vertical, QScrollBar::handle:horizontal {"
               "  background: %1; border-radius: 5px; min-height: 24px; min-width: 24px; }"
               "QScrollBar::add-line, QScrollBar::sub-line {"
               "  background: transparent; border: none; width: 0; height: 0; }"
               "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
               "QAbstractScrollArea::corner { background: transparent; }")
               .arg(border);

  // Fields are underlines, not boxes: the value sits directly on the dialog
  // canvas and a single hairline carries the field, brightening to the accent on
  // focus. No fill and no surrounding frame, matching the application form.
  sheet += QStringLiteral(
               "QLineEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
               "  background: transparent; border: none;"
               "  border-bottom: 1px solid %1; border-radius: 0;"
               "  padding: 4px 2px; selection-background-color: %2; selection-color: %3; }")
               .arg(border, accent, accent_text);
  sheet += QStringLiteral(
               "QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, "
               "QDoubleSpinBox:focus, QComboBox:focus {"
               "  border-bottom: 1px solid %1; }")
               .arg(accent);

  // The drop-down area is part of the field, not a second button: no fill, no
  // divider. The chevron itself is painted by ui::ComboBox / ui::FontComboBox
  // from the palette (Qt's stylesheet engine draws no arrow for a styled
  // drop-down), centred in the 20px column reserved below.
  sheet += QStringLiteral(
               "QComboBox { padding-right: 22px; }"
               "QComboBox::drop-down {"
               "  subcontrol-origin: padding; subcontrol-position: top right;"
               "  width: 20px; border: none; background: transparent; }");

  // An editable combo draws its text through an internal QLineEdit. That child
  // must not add a second underline below the one the combo already owns.
  sheet += QStringLiteral(
      "QComboBox > QLineEdit, QComboBox > QLineEdit:focus {"
      "  background: transparent; border: none; padding: 0; }");

  // The popup is a QListView inside a frame; give it the dialog's own surface so
  // it is never transparent against the window behind it.
  sheet += QStringLiteral(
               "QComboBox QAbstractItemView {"
               "  background: %1; border: 1px solid %2; border-radius: 6px;"
               "  padding: 4px; outline: none;"
               "  selection-background-color: %3; selection-color: %4; }")
               .arg(base, border, accent, accent_text);

  sheet += QStringLiteral(
               "QPushButton#Primary { background: %1; color: %2; border: none;"
               "  border-radius: 6px; padding: 6px 18px; }")
               .arg(accent, accent_text);
  sheet += QStringLiteral("QPushButton#Primary:hover { background: %1; }").arg(accent_hover);
  sheet += QStringLiteral("QPushButton#Primary:pressed { background: %1; }").arg(accent_active);
  sheet += QStringLiteral("QPushButton#Primary:disabled { background: %1; color: %2; }")
               .arg(surface, muted);

  sheet += QStringLiteral(
               "QPushButton#Secondary, QPushButton#RestoreConfig, QPushButton#RestoreMetrics {"
               " background: transparent; color: %1;"
               "  border: 1px solid %2; border-radius: 6px; padding: 6px 14px; }")
               .arg(text, border);
  sheet += QStringLiteral(
               "QPushButton#Secondary:hover, QPushButton#RestoreConfig:hover,"
               " QPushButton#RestoreMetrics:hover { background: %1; }")
               .arg(surface);
  sheet += QStringLiteral(
               "QPushButton#Secondary:disabled, QPushButton#RestoreConfig:disabled,"
               " QPushButton#RestoreMetrics:disabled { color: %1; }")
               .arg(muted);

  sheet += QStringLiteral("QCheckBox { color: %1; }").arg(text);

  return sheet;
}

inline QFont scaled_font(double delta, bool bold) {
  QFont font = ui_font();
  if (font.pointSizeF() > 1.0) {
    font.setPointSizeF(font.pointSizeF() + delta);
  }
  if (bold) {
    font.setWeight(QFont::DemiBold);
  }
  return font;
}

// Sets a label's font from the base UI font and tags it, so a window that is
// cached across shows (the settings window, the running-tasks window) can
// re-derive the font after a font change instead of keeping the one it was built
// with. The tag is a relative size (in points) plus a bold flag.
inline QLabel* apply_derived_font(QLabel* label, double delta, bool bold) {
  label->setProperty("lnbFontDelta", delta);
  label->setProperty("lnbFontBold", bold);
  label->setFont(scaled_font(delta, bold));
  return label;
}

// Re-derives the font of every tagged label in `root` from the current base UI
// font. Labels that were never tagged are left alone.
inline void reapply_derived_fonts(QWidget* root) {
  for (QLabel* label : root->findChildren<QLabel*>()) {
    const QVariant delta = label->property("lnbFontDelta");

    if (!delta.isValid()) {
      continue;
    }

    label->setFont(scaled_font(delta.toDouble(), label->property("lnbFontBold").toBool()));
  }
}

inline QLabel* section_label(QWidget* parent, const QString& text) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("SectionLabel"));
  apply_derived_font(label, 0.0, true);
  // Wrapping keeps a heading from forcing a page wider than its viewport on
  // platforms with wide font metrics, where the settings pages are the tightest
  // fit in the application.
  label->setWordWrap(true);
  return label;
}

inline QLabel* field_label(QWidget* parent, const QString& text) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("FieldLabel"));
  label->setWordWrap(true);
  return label;
}

// Gives a dialog's navigation rail the same comfortable row height as the rest
// of the chrome. The item padding in dialog_stylesheet() alone leaves the rows
// as short as the text, which reads as tiny next to the pages they select; this
// pins a row height derived from the font so it also scales with the UI size.
inline void style_navigation_rail(QListWidget* nav) {
  nav->setFont(ui_font());
  nav->setSpacing(2);

  const int row_height = nav->fontMetrics().height() + 16;

  for (int row = 0; row < nav->count(); ++row) {
    if (QListWidgetItem* item = nav->item(row)) {
      item->setSizeHint(QSize(0, row_height));
    }
  }
}

// Drops the icon the platform style attaches to the standard buttons of a
// QDialogButtonBox. The application's dialogs are text-only: an icon beside
// "Save" or "Cancel" is platform chrome the rest of the window does not share.
inline void strip_button_icons(QDialogButtonBox* box) {
  if (box == nullptr) {
    return;
  }

  for (QAbstractButton* button : box->buttons()) {
    button->setIcon(QIcon());
  }
}

inline QFormLayout* form_layout(QWidget* parent) {
  Q_UNUSED(parent);

  // Built without a parent widget: the caller inserts it into another layout, and
  // installing it on a widget first would make Qt warn about a duplicate layout.
  auto* layout = new QFormLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setHorizontalSpacing(16);
  layout->setVerticalSpacing(10);
  layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
  return layout;
}

// A hairline drawn as a styled QWidget, so it is exactly one pixel and uses the
// theme's border colour.
inline QWidget* h_separator(QWidget* parent) {
  auto* line = new QWidget(parent);
  line->setObjectName(QStringLiteral("Separator"));
  line->setAttribute(Qt::WA_StyledBackground, true);
  line->setFixedHeight(1);
  line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return line;
}

inline QWidget* v_separator(QWidget* parent) {
  auto* line = new QWidget(parent);
  line->setObjectName(QStringLiteral("Separator"));
  line->setAttribute(Qt::WA_StyledBackground, true);
  line->setFixedWidth(1);
  line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  return line;
}

inline QSpinBox* make_spin(QWidget* parent, std::size_t value) {
  // A wheel-safe spin box: crossing the field while scrolling must not change it.
  auto* spin = new SpinBox(parent);
  spin->setRange(0, 1'000'000'000);
  spin->setSingleStep(500);
  spin->setGroupSeparatorShown(true);
  spin->setValue(static_cast<int>(value));
  return spin;
}

} // namespace ui::dialog_style
