#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <utility>
#include <fstream>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <felis/log.hpp>
#include <felis/str.hpp>

#include "store.hpp"

namespace {

std::string string_or(
    const boost::json::object& object, std::string_view key
) {
  const boost::json::value* value = object.if_contains(key);

  return value != nullptr && value->is_string()
      ? std::string(value->as_string())
      : std::string{};
}

std::size_t size_or(
    const boost::json::object& object, std::string_view key, std::size_t fallback
) {
  const boost::json::value* value = object.if_contains(key);

  if (value == nullptr || !value->is_number()) {
    return fallback;
  }

  return static_cast<std::size_t>(value->to_number<double>());
}


std::string normalize_base_url(const std::string& url) {
  std::string text = felis::str::trim(url);

  for (const std::string_view suffix:
        {"/chat/completions", "/completions", "/responses"}) {
    if (text.ends_with(suffix)) {
      text.erase(text.size() - suffix.size());
      break;
    }
  }

  while (!text.empty() && text.back() == '/') {
    text.pop_back();
  }

  return text;
}

std::string protocol_name(felis::bell::api_protocol protocol) {
  return protocol == felis::bell::api_protocol::responses ? "responses" : "chat_completions";
}

felis::bell::api_protocol protocol_from_name(const std::string& name) {
  // Anything unrecognised (including a missing key, read as "") defaults to
  // Responses, so an assistant predating this switch routes through Responses.
  return felis::str::to_lower(felis::str::trim(name)) == "chat_completions"
      ? felis::bell::api_protocol::chat_completions
      : felis::bell::api_protocol::responses;
}

boost::json::object to_json(const felis::bell::assistant& assistant) {
  boost::json::object object;
  object["name"] = assistant.name_;
  object["custom_instructions"] = assistant.custom_instructions_;
  object["api_url"] = assistant.api_url_;
  object["authentication_token"] = assistant.authentication_token_;
  object["model"] = assistant.model_;
  object["proxy"] = assistant.proxy_.has_value()
      ? boost::json::value(*assistant.proxy_)
      : boost::json::value(nullptr);
  object["allow_self_signed_certificates"] =
      assistant.allow_self_signed_certificates_;
  object["search_endpoint"] = assistant.search_endpoint_;
  object["memory"] = assistant.memory_;

  object["turn_timeout_seconds"] =
      static_cast<std::uint64_t>(assistant.turn_timeout_seconds_);
  object["summary_timeout_seconds"] =
      static_cast<std::uint64_t>(assistant.summary_timeout_seconds_);

  // The wire protocol this assistant talks and the reasoning level a new session
  // starts on. Both are written even when defaulted, so the file always states the
  // choice explicitly. Per-session and per-summary reasoning levels live on the
  // session, not here.
  object["protocol"] = protocol_name(assistant.protocol_);
  object["reasoning_effort"] = assistant.reasoning_effort_;

  object["memory_budget"] =
      static_cast<std::uint64_t>(assistant.memory_budget_);
  object["memory_budget_for_session_compact"] =
      static_cast<std::uint64_t>(
        assistant.memory_budget_for_session_compact_
      );
  object["memory_budget_across_messages"] =
      static_cast<std::uint64_t>(
        assistant.memory_budget_across_messages_
      );

  return object;
}


felis::bell::assistant from_json(const boost::json::object& object) {
  felis::bell::assistant assistant;

  assistant.name_ = string_or(object, "name");
  assistant.custom_instructions_ = string_or(object, "custom_instructions");
  assistant.api_url_ = string_or(object, "api_url");
  assistant.authentication_token_ = string_or(object, "authentication_token");
  assistant.model_ = string_or(object, "model");

  if (const boost::json::value* proxy = object.if_contains("proxy");
      proxy != nullptr && proxy->is_string() && !proxy->as_string().empty()) {
    assistant.proxy_ = std::string(proxy->as_string());
  }

  // Absent or non-boolean reads as off: an assistant written before this switch
  // existed must keep verifying certificates.
  if (const boost::json::value* allow_self_signed =
          object.if_contains("allow_self_signed_certificates");
      allow_self_signed != nullptr && allow_self_signed->is_bool()) {
    assistant.allow_self_signed_certificates_ = allow_self_signed->as_bool();
  }

  assistant.memory_ = string_or(object, "memory");
  assistant.search_endpoint_ = string_or(object, "search_endpoint");

  assistant.turn_timeout_seconds_ = size_or(
      object, "turn_timeout_seconds", assistant.turn_timeout_seconds_
  );
  assistant.summary_timeout_seconds_ = size_or(
      object, "summary_timeout_seconds", assistant.summary_timeout_seconds_
  );

  // A missing `protocol` reads as Responses, so an assistant written before the
  // switch existed routes through the Responses API by default. The base URL is
  // normalised so the pre-update full-endpoint value migrates to the base.
  assistant.memory_budget_ = size_or(
      object, "memory_budget", assistant.memory_budget_
  );
  assistant.memory_budget_for_session_compact_ = size_or(
      object, "memory_budget_for_session_compact",
      assistant.memory_budget_for_session_compact_
  );
  assistant.memory_budget_across_messages_ = size_or(
      object, "memory_budget_across_messages",
      assistant.memory_budget_across_messages_
  );

  assistant.protocol_ = protocol_from_name(string_or(object, "protocol"));
  assistant.reasoning_effort_ = string_or(object, "reasoning_effort");

  assistant.api_url_ = normalize_base_url(assistant.api_url_);

  return assistant;
}

} // namespace

/*
 * `uuid` must be valid or this function will throw exceptions
 */
felis::bell::assistant felis::bell::store::load_assistant(const std::string& uuid) const {
  const auto path = this->assistant_settings(uuid);

  std::ifstream in(path, std::ios::binary);

  if (!in) {
    // return assistant{};
    felis::raise("std::ifstream failed to open {}", path.string());
  }

  const std::string text{
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()
  };

  boost::system::error_code error;
  const boost::json::value parsed = boost::json::parse(text, error);

  if (error || !parsed.is_object()) {
    // shark::log::error("{} is not a JSON object", path.string());
    felis::raise("{} is not a JSON object", path.string());
  }

  return from_json(parsed.as_object());
}

void felis::bell::store::save_assistant(const std::string& uuid, const assistant& assistant) const {
  const auto directory = this->assistant_dir(uuid);
  std::error_code error;
  std::filesystem::create_directories(directory, error);

  const auto path = this->assistant_settings(uuid);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);

  if (!out) {
    felis::log::error("cannot write {}", path.string());
    return;
  }

  out << boost::json::serialize(to_json(assistant)) << '\n';
}

bool felis::bell::store::delete_assistant(const std::string& uuid) const {
  // An empty uuid would resolve to the data root itself, so refuse it.
  if (uuid.empty()) {
    return false;
  }

  std::error_code error;
  std::filesystem::remove_all(this->assistant_dir(uuid), error);
  return !error;
}

std::filesystem::path felis::bell::store::data_root() const {
  return this->g_data_root;
}

std::filesystem::path felis::bell::store::global_settings_path() const {
  return this->data_root() / "settings.json";
}


std::vector<std::string> felis::bell::store::scan_assistants() const {
  std::vector<std::string> result;

  std::error_code error;
  const std::filesystem::path root = this->data_root();
  if (!std::filesystem::is_directory(root, error)) {
    return result;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    if (error) {
      break;
    }
    if (!entry.is_directory()) {
      continue;
    }
    if (std::filesystem::exists(entry.path() / "settings.json", error)) {
      result.push_back(entry.path().filename().string());
    }
  }

  std::sort(result.begin(), result.end());
  return result;
}


std::filesystem::path felis::bell::store::assistant_dir(std::string_view uuid) const {
  return data_root() / std::string(uuid);
}

std::filesystem::path felis::bell::store::assistant_settings(std::string_view uuid) const {
  return assistant_dir(uuid) / "settings.json";
}

std::filesystem::path felis::bell::store::assistant_db(std::string_view uuid) const {
  return assistant_dir(uuid) / "data.db";
}

std::filesystem::path felis::bell::store::assistant_log(std::string_view uuid) const {
  return assistant_dir(uuid) / "local-notebook.log";
}

std::filesystem::path felis::bell::store::assistant_lock(std::string_view uuid) const {
  return assistant_dir(uuid) / "lock";
}
