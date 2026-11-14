#include <QApplication>
#include <QDialog>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <felis/async.hpp>

#include "client.hpp"
#include "store.hpp"
#include "felis/log.hpp"
#include "settings/settings.hpp"
#include "support/i18n.hpp"
#include "ui/AssistantPicker.hpp"
#include "ui/MainWindow.hpp"
#include "ui/theme.hpp"
#include "wayland_icon.hpp"

namespace {

// The per-user application data directory, or the --user-data override.
std::filesystem::path resolve_data_root(const QStringList &arguments) {
  for (int i = 1; i < arguments.size(); ++i) {
    const QString &argument = arguments.at(i);

    if (argument == QStringLiteral("--user-data") && i + 1 < arguments.size()) {
      return arguments.at(i + 1).toStdString();
    }

    if (argument.startsWith(QStringLiteral("--user-data="))) {
      return argument.mid(12).toStdString();
    }
  }

  const QString location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  return location.isEmpty() ? std::filesystem::path{"data"}
                            : std::filesystem::path{location.toStdString()};
}

} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Local Notebook"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QApplication::setWindowIcon(ui::application_icon());
  ui::install_wayland_toplevel_icon(app);

  const std::filesystem::path data_root = resolve_data_root(app.arguments());
  felis::log::info("data root: {}", data_root.string());

  felis::bell::store store(data_root);

  lnb::Settings settings = lnb::load_settings(store);

  lnb::i18n::set_language(lnb::i18n::language_from_code(settings.language));
  ui::apply_theme(app, settings);

  // Reopen the last assistant when auto-login is on and it is still on disk;
  // otherwise ask. The picker works off the store; the client, which owns exactly
  // one assistant, is built only after a selection is made.
  std::string uuid = settings.last_assistant;
  const std::vector<std::string> uuids = store.scan_assistants();
  const bool reopen = settings.auto_login && !uuid.empty() &&
                      std::find(uuids.begin(), uuids.end(), uuid) != uuids.end();

  if (!reopen) {
    ui::AssistantPicker picker(store, settings);

    if (picker.exec() != QDialog::Accepted) {
      return 0;
    }

    uuid = settings.last_assistant;
  }

  if (uuid.empty()) {
    return 0;
  }

  // The turn runs on this shared executor; no task ever creates its own thread.
  felis::async::multi_executor<void()> executor(2);

  try {
    felis::bell::client client(std::move(store), uuid);

    ui::MainWindow window(client, settings, executor);
    window.show();

    const int code = app.exec();

    lnb::save_settings(client.get_store(), settings);
    executor.shutdown();
    felis::log::debug("local-notebook exited with code {}", code);
    return code;
  } catch (const std::exception &error) {
    felis::log::error("cannot open assistant {}: {}", uuid, error.what());
    executor.shutdown();
    return 1;
  }
}
