#include "support/OpenFailure.hpp"

#include "support/i18n.hpp"

namespace ui {

QString describe_open_failure(felis::bell::error_code code) {
  switch (code) {
    case felis::bell::error_code::ERR_ASSISTANT_LOCK_FAILED:
      return trs("This assistant is already open in another Local Notebook instance. "
                 "Close it there first, or open a different assistant.");

    default:
      return trs("The assistant could not be opened.");
  }
}

} // namespace ui
