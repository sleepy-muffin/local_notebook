#pragma once

// The model ids the editor offers and the reasoning ladder that goes with them.
//
// The model field stays free text — a gateway may serve an id this list has never
// heard of — but the known ids are listed so the common cases are one click away.
// The reasoning selector is derived from the same table on both sides: the
// assistant form and the composer, so the two can never drift apart.

#include <string>
#include <vector>

namespace ui::known_models {

// Known model ids, in the order they are offered.
[[nodiscard]] std::vector<std::string> ids();

// The reasoning levels a model offers, in display order. An unknown model gets
// the generic ladder.
[[nodiscard]] std::vector<std::string> reasoning_levels(const std::string& model);

// The level a model starts on when nothing is stored yet. An unknown model gets
// the generic default.
[[nodiscard]] std::string default_reasoning(const std::string& model);

} // namespace ui::known_models
