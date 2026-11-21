#pragma once

#include <string>
#include <optional>
#include <vector>

#include <stop_token>

namespace felis::bell{
// struct SearchSettings {
//   // The base URL of the SearXNG-compatible service, e.g.
//   // "http://127.0.0.1:8080". Empty means the user has not configured search, so
//   // web_search reports that instead of guessing an endpoint.
//   std::string endpoint;
// };



// `stop` is polled by the HTTP transport while the request is in flight, so a
// cancelled turn does not keep a worker pinned until the read timeout. Failure
// (a refused connection, a 4xx/5xx) is still reported as a value, not thrown.
std::string fetch_url(
    const std::string& url, const std::optional<std::string>& proxy,
    std::stop_token stop = {});
std::string web_search(
    const std::string& query, const std::optional<std::string>& proxy,
    const std::string& endpoint, std::stop_token stop = {});
}
