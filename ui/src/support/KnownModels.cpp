#include "support/KnownModels.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace ui::known_models {

namespace {

struct Entry {
  const char* id;
  const char* default_reasoning;
  const char* const* levels;
  std::size_t level_count;
};

// DeepSeek V4.1 Flash also accepts a numeric effort from 1 to 100; the field is
// editable, so such a value can be typed even though the popup only lists the
// named levels.
constexpr const char* kDeepseekFlashLevels[] = {"none", "low", "high", "max"};

// GPT-6.1 Sol has no unrestricted "none": its floor is "low".
constexpr const char* kSolLevels[] = {"low", "medium", "high", "xhigh", "max"};

constexpr const char* kLunaLevels[] = {
    "none", "low", "medium", "high", "xhigh", "max"};

// The ladder for a model the table has not listed.
constexpr const char* kGenericLevels[] = {"none", "low", "medium", "high"};
constexpr const char* kGenericDefault = "medium";

// The ids the editor lists, each with the ladder that model actually supports.
// A model absent here still works: the ladder and the default fall back to the
// generic ones above.
constexpr Entry kModels[] = {
    {"deepseek-flash", "low", kDeepseekFlashLevels, std::size(kDeepseekFlashLevels)},
    {"deepseek-pro", "medium", kGenericLevels, std::size(kGenericLevels)},
    {"gpt-6-luna", "medium", kLunaLevels, std::size(kLunaLevels)},
    {"gpt-6.1-sol", "medium", kSolLevels, std::size(kSolLevels)},
};

const Entry* find(const std::string& model) {
  const auto it = std::find_if(
      std::begin(kModels), std::end(kModels),
      [&model](const Entry& entry) { return model == entry.id; });
  return it == std::end(kModels) ? nullptr : it;
}

} // namespace

std::vector<std::string> ids() {
  std::vector<std::string> result;
  result.reserve(std::size(kModels));

  for (const Entry& entry: kModels) {
    result.emplace_back(entry.id);
  }

  return result;
}

std::vector<std::string> reasoning_levels(const std::string& model) {
  const Entry* entry = find(model);

  const char* const* levels = entry != nullptr ? entry->levels : kGenericLevels;
  const std::size_t count =
      entry != nullptr ? entry->level_count : std::size(kGenericLevels);

  return std::vector<std::string>(levels, levels + count);
}

std::string default_reasoning(const std::string& model) {
  const Entry* entry = find(model);
  return entry != nullptr ? entry->default_reasoning : kGenericDefault;
}

} // namespace ui::known_models
