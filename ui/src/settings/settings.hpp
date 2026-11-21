#pragma once

#include <array>
#include <optional>
#include <string>

#include "settings/metrics.hpp"
#include "store.hpp"

namespace lnb {

// App-global settings, shared by every assistant, persisted at
// data/settings.json (design-1.md §8). Assistant- and session-level settings
// live elsewhere.
struct Settings {
  // UUID of the assistant to reopen at startup; empty on first run.
  std::string last_assistant;

  // When true, the last assistant is reopened automatically at startup; when
  // false the assistant picker is always shown. Toggled from the picker and
  // from the Account page of the settings window.
  bool auto_login = true;

  std::string theme = "Catppuccin Mocha";

  // UI language code: "en_us", "zh_cn", "zh_tw", "ja_jp" or "de_de". Applied
  // at startup (and
  // on Save from the Configuration page); see support/i18n.hpp. This value is
  // always explicit; a missing stored value is resolved through
  // default_language_code().
  std::string language = "en_us";

  // Optional background image, resolved relative to the working directory.
  std::optional<std::string> background_url;

  // RGBA colour painted over the background image. nullopt shows it untinted.
  std::optional<std::array<float, 4>> background_override =
      std::array<float, 4>{0.094f, 0.098f, 0.149f, 0.85f};

  // General UI font (every widget), chat font (transcript + composer) and
  // monospace font (code). Carlito and JetBrains Mono are the defaults; an
  // empty family keeps the system default for that role.
  std::string ui_font = "Carlito";
  int ui_font_size = 11;
  std::string font;
  int font_size = 12;
  std::string mono_font = "JetBrains Mono";
  int mono_font_size = 11;

  // Monospace text scale as a fraction of its surrounding size. It applies to
  // every mono run: inline code (scaled from the chat font) and fenced code
  // blocks (scaled from the monospace size).
  float mono_scale = 0.9f;

  // Tool-call/result strip size as a fraction of the prose size. The strip is
  // deliberately subtle, so it draws smaller than the messages it annotates.
  float tool_text_scale = 0.88f;

  // When true, Enter inserts a newline and Ctrl+Enter sends; when false the
  // reverse.
  bool enter_with_modifier = true;

  // Transcript rendering window. Only the most recent messages are rendered at
  // once; older ones are revealed a page at a time on demand. This bounds
  // rendering only — the full history is still loaded and sent.
  int rendered_message_limit = 20;
  int history_page_size = 20;

  // Automatic re-attempt of a turn that stopped for a transport or stream
  // reason. `retry_max_attempts` counts re-attempts after the first (0 disables
  // retrying); `retry_interval_ms` is the wait between them. Read by
  // `ConversationController` for every send.
  int retry_max_attempts = 1;
  int retry_interval_ms = 1000;

  // Base64 window geometry (Qt saveGeometry), restored on the next launch.
  std::optional<std::string> geometry;

  // Theme metric overrides. nullopt keeps the theme's own defaults.
  std::optional<ThemeMetrics> metrics;
};

// data/settings.json. A missing or malformed file yields the defaults.
Settings load_settings(const felis::bell::store& store);
void save_settings(const felis::bell::store& store, const Settings& settings);

// The language to use before the user has chosen one, derived from the system
// locale: "zh_cn"/"zh_tw" for Chinese, "ja_jp" for Japanese, "de_de" for
// German, and "en_us" otherwise.
// This is also what "Restore to defaults" puts back in the language selector.
std::string default_language_code();

} // namespace lnb
