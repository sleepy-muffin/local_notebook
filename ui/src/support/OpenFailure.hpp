#pragma once

#include <QString>

#include "common.hpp"

namespace ui {

// The sentence shown when `client::open_assistant()` failed. Only the code comes
// in: the detail behind a refusal (the pid holding the lock, or the filesystem
// error) stays in the log, so this stays a pure code-to-text mapping.
QString describe_open_failure(felis::bell::error_code code);

} // namespace ui
