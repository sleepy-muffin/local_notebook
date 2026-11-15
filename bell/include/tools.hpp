#pragma once

#include "common.hpp"
#include "message.hpp"

#include <boost/json.hpp>

#include <felis/net/llm.hpp>

#include <stop_token>

namespace felis::bell{

boost::json::array tool_definitions(const tool_capabilities& capabilities);
boost::json::array responses_tools(const felis::bell::tool_capabilities& capabilities);

// `stop` is polled while a long-running tool (the command tool) is executing, so
// a cancelled turn does not leave a child process behind. The optional
// parameter keeps callers that never cancel from having to pass one.
std::string execute_local_tool(
    const felis::net::llm::ToolCall& call, const std::vector<std::string>& shared_directories,
    const std::optional<std::string>& proxy, const felis::bell::tool_capabilities& permissions,
    std::stop_token stop = {}
);

}
