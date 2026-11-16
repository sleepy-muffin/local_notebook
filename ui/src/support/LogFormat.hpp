#pragma once

// Presentation helpers for the log inspector. They live in their own file rather
// than in the window's anonymous namespace so the level naming, colouring and
// row formatting can be reused by any future log-facing widget.

#include <QColor>
#include <QString>

#include <felis/log.hpp>

namespace ui::log_format {

// The level's translated name: "Info", "Debug" or "Error".
QString level_name(felis::log::message_type type);

// The level's colour in the active theme: errors carry the danger colour, debug
// lines recede to the muted one, and info reads as ordinary text.
QColor level_colour(felis::log::message_type type);

// The local wall-clock time of a captured record, "HH:mm:ss". Safe to call from
// the logger's worker thread.
QString current_timestamp();

// A single-line preview for the list: the first line of the body, truncated to
// `limit` characters (with an ellipsis). Tabs collapse to spaces so a row keeps
// its height.
QString preview(const QString& body, int limit = 160);

} // namespace ui::log_format
