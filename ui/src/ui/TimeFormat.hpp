#pragma once

#include <QString>

#include <string>

namespace ui {

// Formats a stored ISO-8601 UTC message time for display: the clock time for
// today, the month and day within the current year, and the full date otherwise.
// Returns an empty string when `iso` is empty and echoes it back when it cannot
// be parsed.
QString format_message_time(const std::string& iso);

} // namespace ui
