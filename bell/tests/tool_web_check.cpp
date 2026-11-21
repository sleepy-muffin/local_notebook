// Checks that a failing network tool degrades into an ordinary tool result and
// that cancellation reaches the HTTP transport.
//
// A transport failure (here: a refused loopback connection) must not throw out
// of `execute_local_tool`: the turn loop relies on a `Tool error: …` string so
// the model can retry instead of the stream ending. Cancellation must be
// reported as such, and must be observable by the transport itself.

#include "tools.hpp"
#include "web_tools.hpp"

#include <chrono>
#include <cstdio>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
  std::fflush(stdout);
  if (!ok) ++failures;
}

static felis::net::llm::ToolCall fetch_call(std::string url) {
  felis::net::llm::ToolCall call;
  call.id = "call-1";
  call.name = "fetch_url";
  call.arguments = "{\"url\": \"" + std::move(url) + "\"}";
  return call;
}

int main() {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  using felis::bell::tool_capability;
  using felis::bell::tool_capabilities;

  const tool_capabilities online(
      static_cast<std::uint32_t>(tool_capability::online));
  const tool_capabilities offline(
      static_cast<std::uint32_t>(tool_capability::repo_read));

  // Port 1 on loopback refuses immediately: no server and no DNS needed.
  const std::string refused = "http://127.0.0.1:1/nope";

  {
    const std::string out = felis::bell::execute_local_tool(
        fetch_call(refused), {}, std::nullopt, online, std::nullopt);
    check(out.rfind("Tool error:", 0) == 0,
          "a refused fetch becomes a Tool error result");
    check(out.find("Tool error:") == 0 && out.size() > 12,
          "the result carries a reason for the model");
  }

  {
    const std::string out = felis::bell::execute_local_tool(
        fetch_call(refused), {}, std::nullopt, offline, std::nullopt);
    check(out.find("disabled") != std::string::npos,
          "fetch_url refused without the online capability");
  }

  {
    std::stop_source source;
    source.request_stop();

    const std::string out = felis::bell::execute_local_tool(
        fetch_call(refused), {}, std::nullopt, online, std::nullopt,
        source.get_token());
    check(out.find("cancelled") != std::string::npos,
          "execute_local_tool reports cancellation, not a fetch failure");
  }

  {
    // Proves the token reaches the transport: a pre-requested stop must abort
    // before the request starts rather than waiting out the read timeout.
    std::stop_source source;
    source.request_stop();

    const auto began = std::chrono::steady_clock::now();
    bool threw = false;

    try {
      felis::bell::fetch_url(refused, std::nullopt, source.get_token());
    } catch (const std::exception&) {
      threw = true;
    }

    const auto elapsed = std::chrono::steady_clock::now() - began;
    check(threw, "the transport observes the stop token");
    check(elapsed < std::chrono::seconds(5), "cancellation is prompt");
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
