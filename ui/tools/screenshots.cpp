// Renders the windows the README and docs/architecture.md embed.
//
//   local_notebook_screenshots docs/screenshots
//
// It needs no display server and no endpoint: the store, the assistant and its
// sessions are seeded into a scratch directory, the real widgets are built on
// top of them, and what they paint is saved. Every sample carries its theme in
// the file name — main-window-light.png, main-window-dark.png — so the files say
// which theme they show rather than relying on which one happens to be
// unsuffixed. README.md references the light pair.
//
// The palette, stylesheet and fonts installed by apply_theme() are application
// wide and widgets read them when they are constructed, so a sample is rendered
// from scratch: apply the theme while no window exists, create it, capture it,
// then let it go before the next theme is applied. Capturing two themes from one
// window would only relabel the first one's pixels.

#include <QApplication>
#include <QSize>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <felis/async.hpp>
#include <felis/str.hpp>

#include "client.hpp"
#include "settings/settings.hpp"
#include "store.hpp"
#include "support/i18n.hpp"
#include "ui/MainWindow.hpp"
#include "ui/theme.hpp"

namespace screenshots {

void shoot(QWidget &widget, const std::filesystem::path &path, const QSize &size) {
  widget.resize(size);
  widget.show();

  // The first pass lays the widgets out, the second paints them at the size they
  // settled on.
  QApplication::processEvents();
  QApplication::processEvents();

  if (!widget.grab().save(QString::fromStdString(path.string()))) {
    std::cerr << "failed to write " << path << '\n';
    std::exit(EXIT_FAILURE);
  }

  std::cout << "wrote " << path << '\n';
}

// One rendered sample: the theme to apply and the suffix its files carry.
struct Sample {
  const char *theme;
  const char *suffix;
};

std::string assistant_uuid() { return "a1b2c3d4-0000-0000-0000-000000000001"; }

felis::bell::assistant make_assistant() {
  felis::bell::assistant assistant;
  assistant.name_ = "Aurora";
  assistant.model_ = "gpt-5.6-luna";
  assistant.api_url_ = "https://api.example.invalid/v1";
  assistant.custom_instructions_ = "You are a careful engineering assistant. Answer concisely and precisely, "
                                   "and prefer showing the smallest correct change.";
  assistant.memory_ = "The user is building Local Notebook, a C++23 / Qt Widgets application. "
                      "They prefer British spelling in comments and flat, Zed-like interfaces "
                      "with no rounded chrome.";
  return assistant;
}

std::uint32_t add_session(felis::bell::client &client, const std::string &title, bool pinned = false) {
  return client.create_session(felis::bell::shadowed_session(0, title, pinned)).get();
}

void add_message(
    felis::bell::client &client, std::uint32_t session_id, felis::bell::message_kind kind, const std::string &text) {
  client.append_message(session_id, felis::bell::shadowed_message(text, kind));
}

// The sessions behind the featured one, so the sidebar reads like a notebook
// someone has been using.
void seed_background(felis::bell::client &client) {
  const std::uint32_t performance = add_session(client, "Qt performance");

  add_message(client, performance, felis::bell::message_kind::user_prompt, "How do I keep a long chat list smooth?");
  add_message(
      client, performance, felis::bell::message_kind::assistant_output,
      "Render only what is visible, and reuse the rows as they scroll.");

  const std::uint32_t notes = add_session(client, "Release notes", /*pinned=*/true);

  add_message(
      client, notes, felis::bell::message_kind::user_prompt,
      "Draft the 0.6.0 entry. Keep it to the behaviour that changed.");
  add_message(
      client, notes, felis::bell::message_kind::assistant_output,
      "0.6.0 adds Chat Completions alongside Responses, and reports an "
      "assistant that is already open instead of exiting.");
}

// The featured session is seeded last, so its newest message is what the main
// window opens on.
void seed_featured(felis::bell::client &client) {
  const std::uint32_t design = add_session(client, "Summarisation design");

  add_message(
      client, design, felis::bell::message_kind::user_prompt,
      "Why can a long conversation truncate the model's reply?");
  add_message(
      client, design, felis::bell::message_kind::assistant_output,
      "Because the completion budget covers both hidden reasoning and the visible "
      "answer. If the summarisation budget is too small, the visible answer can be "
      "cut off.\n\n"
      "```cpp\nconst std::size_t max_tokens = session.auto_compact_budget_;\n```\n\n"
      "The fix is to raise the summarisation budget, not the answer budget.");
  add_message(
      client, design, felis::bell::message_kind::user_prompt,
      "Should the input bound be derived from the model context window?");
  add_message(
      client, design, felis::bell::message_kind::assistant_output,
      "No. Deriving it creates a second source of truth. The input bound is the "
      "session's own `auto_compact_threshold_`; nothing else feeds it.");
}

void capture(
    QApplication &app, felis::bell::client &client, lnb::Settings &settings,
    felis::async::multi_executor<void()> &executor, const std::filesystem::path &output, const Sample &sample) {
  settings.theme = sample.theme;
  ui::apply_theme(app, settings);

  {
    ui::MainWindow window(client, settings, executor);
    shoot(window, output / ("main-window" + std::string(sample.suffix) + ".png"), QSize(1180, 760));
  }
}

} // namespace screenshots

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: local_notebook_screenshots <output-directory>\n";
    return EXIT_FAILURE;
  }

  const std::filesystem::path output = std::filesystem::absolute(argv[1]);

  // Everything the application keeps on disk — the database, the settings, the
  // log — lands in a scratch directory that goes away with the process. The
  // working directory moves there too, so nothing is written beside the sources.
  const std::filesystem::path scratch =
      std::filesystem::temp_directory_path() / ("lnb-screenshots-" + felis::str::make_uuid());

  std::filesystem::create_directories(scratch);
  std::filesystem::create_directories(output);
  std::filesystem::current_path(scratch);

  // The published screenshots are captured at twice the logical size: GitHub
  // downscales them when the README is displayed, and the text stays crisp there.
  // An explicit scale in the environment wins, so a caller can ask for 1:1.
  if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
    qputenv("QT_SCALE_FACTOR", "2");
  }

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Local Notebook"));
  QApplication::setApplicationVersion(QStringLiteral(LOCAL_NOTEBOOK_VERSION));
  QApplication::setWindowIcon(ui::application_icon());

  lnb::Settings settings;

  felis::bell::store store(scratch / "data");
  const std::string uuid = screenshots::assistant_uuid();
  store.save_assistant(uuid, screenshots::make_assistant());

  // `client` owns its store, so the store above is only the seeding hand-off.
  // Construction is the open; a refusal unwinds instead of yielding half a
  // client.
  std::unique_ptr<felis::bell::client> client;
  try {
    client = std::make_unique<felis::bell::client>(std::move(store), uuid);
  } catch (const felis::bell::open_error& error) {
    std::cerr << "cannot open the scratch assistant (code "
              << static_cast<std::uint32_t>(error.code()) << "); see its log\n";
    return EXIT_FAILURE;
  }

  screenshots::seed_background(*client);
  screenshots::seed_featured(*client);

  lnb::i18n::set_language(lnb::i18n::language_from_code(settings.language));

  felis::async::multi_executor<void()> executor(2);

  // "Catppuccin Latte" is the theme the published screenshots were taken with, so
  // it keeps the names README.md references; "Catppuccin Mocha" is the
  // application default, shown beside it.
  const screenshots::Sample samples[] = {
      {"Catppuccin Latte", "-light"},
      {"Catppuccin Mocha", "-dark"},
  };

  for (const screenshots::Sample &sample: samples) {
    screenshots::capture(app, *client, settings, executor, output, sample);
  }

  executor.shutdown();

  return EXIT_SUCCESS;
}
