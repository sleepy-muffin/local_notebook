#pragma once

#include <string>
#include <optional>
#include <vector>

namespace felis::bell{
// struct SearchSettings {
//   // The base URL of the SearXNG-compatible service, e.g.
//   // "http://127.0.0.1:8873". Empty means the user has not configured search, so
//   // web_search reports that instead of guessing an endpoint.
//   std::string endpoint;
// };



std::string fetch_url(const std::string& url, const std::optional<std::string>& proxy);
std::string web_search(
    const std::string& query, const std::optional<std::string>& proxy,
    const std::string& endpoint);
}
