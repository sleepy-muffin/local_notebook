// Manual check for the command tool wiring: schema gating and execution.
//
// Not part of the build; compile it against the built static libraries, e.g.
//
//   g++ -std=c++23 -Ibell -Ibell/include -Iexternal/felis/include \
//     bell/tests/tool_command_check.cpp build/linux-x64-debug/bell/libfelis_bell.a \
//     external/felis/libfelis-net-linux-x86_64.a \
//     external/felis/libfelis-sqlite3-linux-x86_64.a \
//     external/felis/libfelis-common-linux-x86_64.a \
//     -lboost_json -lboost_container -lboost_regex -lboost_url -lssl -lcrypto \
//     -pthread -o /tmp/tool_command_check

#include "tools.hpp"

#include <boost/json.hpp>

#include <cstdio>
#include <stop_token>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  std::printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
  std::fflush(stdout);
  if (!ok) ++failures;
}

static felis::net::llm::ToolCall command_call(std::string arguments) {
  felis::net::llm::ToolCall call;
  call.id = "call-1";
  call.name = "run_command";
  call.arguments = std::move(arguments);
  return call;
}

int main() {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  using felis::bell::tool_capability;
  using felis::bell::tool_capabilities;

  // Gating: offered only with the terminal capability.
  const tool_capabilities files_only(static_cast<std::uint32_t>(tool_capability::repo_read));
  const tool_capabilities with_terminal(
      static_cast<std::uint32_t>(tool_capability::repo_read) |
      static_cast<std::uint32_t>(tool_capability::terminal));

  {
    bool found = false;
    for (const boost::json::value& tool : felis::bell::tool_definitions(files_only)) {
      const boost::json::object& function = tool.at("function").as_object();
      if (function.at("name").as_string() == "run_command") found = true;
    }
    check(!found, "run_command hidden without terminal");
  }
  {
    bool found = false;
    for (const boost::json::value& tool : felis::bell::tool_definitions(with_terminal)) {
      const boost::json::object& function = tool.at("function").as_object();
      if (function.at("name").as_string() != "run_command") continue;
      found = true;
      const boost::json::object& parameters = function.at("parameters").as_object();
      check(parameters.at("required").as_array().size() == 1,
            "command is the only required argument");
      check(parameters.at("properties").as_object().contains("timeout_seconds"),
            "timeout_seconds documented");
    }
    check(found, "run_command offered with terminal");
  }

  const std::vector<std::string> roots{"/tmp"};

  {
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"command": "printf 'hi from %s' \"$PWD\""})"), roots, std::nullopt,
        with_terminal);
    check(out.find("exit code: 0") != std::string::npos && out.find("/tmp") != std::string::npos &&
              out.find("hi from") != std::string::npos,
          "runs in the first shared directory");
  }
  {
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"command": "exit 7"})"), roots, std::nullopt, with_terminal);
    check(out.find("exit code: 7") != std::string::npos, "exit code reaches the model");
  }
  {
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"command": "true"})"), roots, std::nullopt, files_only);
    check(out.find("disabled") != std::string::npos, "refused without the capability");
  }
  {
    std::stop_source source;
    source.request_stop();
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"command": "sleep 30"})"), roots, std::nullopt, with_terminal,
        source.get_token());
    check(out.find("cancelled") != std::string::npos, "stop token cancels the command");
  }
  {
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"command": "sleep 30", "timeout_seconds": 1})"), roots, std::nullopt,
        with_terminal);
    check(out.find("timeout") != std::string::npos, "timeout_seconds honoured");
  }
  {
    const std::string out = felis::bell::execute_local_tool(
        command_call(R"({"timeout_seconds": 5})"), roots, std::nullopt, with_terminal);
    check(out.find("Missing command") != std::string::npos, "empty command rejected");
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
