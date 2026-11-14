#pragma once

#include "common.hpp"
#include "message.hpp"

#include <boost/json.hpp>

#include <felis/net/llm.hpp>

namespace felis::bell{

boost::json::array tool_definitions(const tool_capabilities& capabilities);
boost::json::array responses_tools(const felis::bell::tool_capabilities& capabilities);
std::string execute_local_tool(
    const felis::net::llm::ToolCall& call, const std::vector<std::string>& shared_directories,
    const std::optional<std::string>& proxy, const felis::bell::tool_capabilities& permissions
);

}
