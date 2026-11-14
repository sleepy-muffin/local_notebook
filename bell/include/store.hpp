#pragma once

#include <filesystem>
#include <vector>

namespace felis::bell{
// bell store

enum class api_protocol : std::uint8_t {
  chat_completions = 0,
  responses = 1,
};
class assistant {
public:
  std::string name_;
  std::string custom_instructions_;
  std::string api_url_;
  std::string authentication_token_;
  std::string model_;
  std::optional<std::string> proxy_;
  api_protocol protocol_ = api_protocol::responses;

  // Where web_search sends its queries: the base URL of a SearXNG-compatible
  // service, e.g. "http://127.0.0.1:8873" for the bundled browser bridge
  // (scripts/search_bridge.py). Empty means the user has not configured search;
  // web_search then reports that to the model instead of failing.
  std::string search_endpoint_;

  // Request windows, in seconds. A streaming turn may wait this long for the
  // next response byte before the request is abandoned; summarisation gets its
  // own, longer window because a reasoning model can stay silent while folding
  // a large transcript. Both live in the assistant's settings.json.
  std::size_t turn_timeout_seconds_ = 180;
  std::size_t summary_timeout_seconds_ = 420;

  // The level a new session of this assistant starts on. It is not what normal
  // chat turns use, and not what the summarisers run at.
  std::string reasoning_effort_;


  std::string memory_;
  std::size_t memory_budget_ = 4000;
  std::size_t memory_threshold_ = 80000;
  std::size_t memory_budget_for_session_compact_ = 16000;
};

class store {

public:
  store(std::filesystem::path root) {
    g_data_root = root.empty() ? std::filesystem::path{"data"} : std::move(root);
  }

  store(const store&) = delete;
  store& operator=(const store&) = delete;

  store(store&&) = default;
  store& operator=(store&&) = default;

  [[nodiscard]] std::filesystem::path data_root() const;

  [[nodiscard]] std::filesystem::path global_settings_path() const;

  [[nodiscard]] std::vector<std::string> scan_assistants() const;
  [[nodiscard]] assistant load_assistant(const std::string &uuid) const;


  [[nodiscard]] std::filesystem::path assistant_dir(std::string_view uuid) const;
  [[nodiscard]] std::filesystem::path assistant_settings(std::string_view uuid) const;
  [[nodiscard]] std::filesystem::path assistant_db(std::string_view uuid);
  [[nodiscard]] std::filesystem::path assistant_log(std::string_view uuid);

  // The instance lock held while this process has the assistant open. It is
  // deliberately never deleted; see instance_lock for why.
  [[nodiscard]] std::filesystem::path assistant_lock(std::string_view uuid);

  void save_assistant(const std::string &uuid, const assistant &assistant) const;

  // Removes the assistant's whole on-disk directory (settings, database, log).
  // Returns false for an empty uuid or when the directory could not be removed.
  bool delete_assistant(const std::string &uuid) const;

private:
  // Mutable so main() can redirect the whole tree. Defaults to the historical
  // working-directory-relative location that the tests and the screenshot tool rely
  // on; the application overrides it before constructing the Application.
  std::filesystem::path g_data_root{"data"};

};

}
