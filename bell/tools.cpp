#include <felis/file/process.hpp>

#include "tools.hpp"

namespace {


// Builds one property object.
boost::json::object property(std::string type, std::string description) {
  boost::json::object result;
  result["type"] = std::move(type);

  if (!description.empty()) {
    result["description"] = std::move(description);
  }

  return result;
}

boost::json::object chat_tool(boost::json::object function) {
  boost::json::object wrapper;
  wrapper["type"] = "function";
  wrapper["function"] = std::move(function);
  return wrapper;
}

}

boost::json::array felis::bell::tool_definitions(const tool_capabilities& capabilities) {
  if (capabilities.empty()) {
    return {};
  }

  // Filesystem tools: only offered when the session has a shared directory.
  //
  // The model sees a synthetic tree of relative paths, never the configured
  // directories themselves. A single directory is mounted at the root; when
  // several are configured each gets a visible top-level name.
  const std::string path_help =
      "A path relative to the configured shared directories. When several "
      "directories are configured, the first segment names one of them; call "
      "list_directory with no path to see the names. Absolute paths are not "
      "accepted.";

  boost::json::object list_function;
  list_function["name"] = "list_directory";
  list_function["description"] =
      "List files and subdirectories immediately inside a directory of the "
      "configured shared directories. Directories end with /. Call it with no "
      "arguments to list the top level: the configured directories when there "
      "are several, or the contents of the single one.";
  {
    boost::json::object properties;
    properties["path"] = property(
        "string", "Directory to list. " + path_help + " Omit to list the top level."
    );

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{};
    parameters["additionalProperties"] = false;

    list_function["parameters"] = std::move(parameters);
  }

  boost::json::object search_function;
  search_function["name"] = "search_code";
  search_function["description"] =
      "Search text files in the configured shared directories. Returns matching "
      "file paths, line numbers, and surrounding context. Matching is "
      "case-insensitive unless the query contains an uppercase letter; binary "
      "files are skipped.";
  {
    boost::json::object properties;
    properties["query"] = property("string", "Symbol name or text to search for");
    properties["path"] = property(
        "string",
        "Directory to search. " + path_help + " Omit to search every configured directory."
    );

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"query"};
    parameters["additionalProperties"] = false;

    search_function["parameters"] = std::move(parameters);
  }

  boost::json::object search_regex_function;
  search_regex_function["name"] = "search_code_regex";
  search_regex_function["description"] =
      "Search text files in the configured shared directories with a regular "
      "expression (PCRE syntax), honouring .gitignore/.ignore files and "
      "skipping hidden entries. Returns matching file paths, line numbers and "
      "surrounding context. Matching is case-sensitive; add an inline flag "
      "such as (?i) for case-insensitive matching, and \\b for word "
      "boundaries.";
  {
    boost::json::object properties;
    properties["pattern"] =
        property("string", "Regular expression to search for");
    properties["path"] = property(
        "string",
        "Directory to search. " + path_help + " Omit to search every configured directory."
    );
    properties["glob"] = property(
        "string",
        "Optional glob to restrict which files are searched, e.g. \"*.cpp\" or "
        "\"src/**/*.hpp\"."
    );

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"pattern"};
    parameters["additionalProperties"] = false;

    search_regex_function["parameters"] = std::move(parameters);
  }

  boost::json::object find_files_function;
  find_files_function["name"] = "find_files";
  find_files_function["description"] =
      "List files in the configured shared directories whose path matches a "
      "glob, honouring .gitignore/.ignore files and skipping hidden entries. "
      "Use it to discover files by name or extension without reading their "
      "contents.";
  {
    boost::json::object properties;
    properties["glob"] = property(
        "string", "Glob to match file paths, e.g. \"*.md\" or \"src/**/*.cpp\"."
    );
    properties["path"] = property(
        "string",
        "Directory to search. " + path_help + " Omit to search every configured directory."
    );

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"glob"};
    parameters["additionalProperties"] = false;

    find_files_function["parameters"] = std::move(parameters);
  }

  boost::json::object read_function;
  read_function["name"] = "read_file";
  read_function["description"] =
      "Read a limited range of lines from a file in the configured shared "
      "directories.";
  {
    boost::json::object properties;
    properties["path"] = property(
        "string", "File to read. " + path_help
    );
    properties["start_line"] = property("integer", "");
    properties["end_line"] = property("integer", "");

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] =
        boost::json::array{"path", "start_line", "end_line"};
    parameters["additionalProperties"] = false;

    read_function["parameters"] = std::move(parameters);
  }

  boost::json::object write_function;
  write_function["name"] = "write_file";
  write_function["description"] =
      "Create or overwrite a text file in the configured shared directories. "
      "The parent directory must already exist. The whole file is replaced, so "
      "use edit_file for a targeted change.";
  {
    boost::json::object properties;
    properties["path"] = property("string", "File to write. " + path_help);
    properties["content"] = property("string", "Full file content to write");

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"path", "content"};
    parameters["additionalProperties"] = false;

    write_function["parameters"] = std::move(parameters);
  }

  boost::json::object edit_function;
  edit_function["name"] = "edit_file";
  edit_function["description"] =
      "Replace one exact occurrence of old_text with new_text in a file in the "
      "configured shared directories. Fails when the text is absent or occurs "
      "more than once, so include enough surrounding context to make it unique.";
  {
    boost::json::object properties;
    properties["path"] = property("string", "File to edit. " + path_help);
    properties["old_text"] = property("string", "Exact text to replace");
    properties["new_text"] = property("string", "Replacement text");

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] =
        boost::json::array{"path", "old_text", "new_text"};
    parameters["additionalProperties"] = false;

    edit_function["parameters"] = std::move(parameters);
  }

  boost::json::object delete_function;
  delete_function["name"] = "delete_file";
  delete_function["description"] =
      "Delete one file in the configured shared directories. Directories are "
      "never removed, and deletion cannot be undone. Use it only when the user "
      "asked for the file to be removed.";
  {
    boost::json::object properties;
    properties["path"] = property("string", "File to delete. " + path_help);

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"path"};
    parameters["additionalProperties"] = false;

    delete_function["parameters"] = std::move(parameters);
  }

  // Network tools: always offered, they work without a shared directory.
  boost::json::object fetch_function;
  fetch_function["name"] = "fetch_url";
  fetch_function["description"] =
      "Fetch an http/https URL and return its visible text. Use it to read a "
      "web page referenced by the user. HTML is converted to plain text.";
  {
    boost::json::object properties;
    properties["url"] =
        property("string", "Absolute http:// or https:// URL to fetch");

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"url"};
    parameters["additionalProperties"] = false;

    fetch_function["parameters"] = std::move(parameters);
  }

  boost::json::object web_search_function;
  web_search_function["name"] = "web_search";
  web_search_function["description"] =
      "Search the web and return the top results with their titles, URLs and "
      "snippets. Use it to find sources online; then use fetch_url to read a "
      "result in full.";
  {
    boost::json::object properties;
    properties["query"] = property("string", "Search terms");

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"query"};
    parameters["additionalProperties"] = false;

    web_search_function["parameters"] = std::move(parameters);
  }

  // Command tool: the line is handed to the platform shell, so pipes,
  // redirection and quoting behave the way a user expects. Not sandboxed; the
  // session's "Run commands" checkbox is the consent gate.
  boost::json::object run_function;
  run_function["name"] = "run_command";
  run_function["description"] =
      "Run a command line through " + felis::process::shell_name() +
      " and return its combined output and exit code. The working directory is "
      "the session's first shared directory. Use it to build, test or inspect "
      "the project. Commands are not sandboxed and are killed when the turn is "
      "cancelled.";
  {
    boost::json::object properties;
    properties["command"] = property(
        "string",
        "Command line to run. Pipes, redirection and quoting are interpreted "
        "by the shell, not by this tool."
    );
    properties["timeout_seconds"] = property(
        "integer",
        "Seconds to wait before the command is killed. Defaults to 120, "
        "clamped to 1..600."
    );

    boost::json::object parameters;
    parameters["type"] = "object";
    parameters["properties"] = std::move(properties);
    parameters["required"] = boost::json::array{"command"};
    parameters["additionalProperties"] = false;

    run_function["parameters"] = std::move(parameters);
  }

  boost::json::array tools;

  if (capabilities.repo_read()) {
    tools.emplace_back(chat_tool(std::move(list_function)));
    tools.emplace_back(chat_tool(std::move(search_function)));
    tools.emplace_back(chat_tool(std::move(search_regex_function)));
    tools.emplace_back(chat_tool(std::move(find_files_function)));
    tools.emplace_back(chat_tool(std::move(read_function)));
  }

  if (capabilities.repo_write()) {
    tools.emplace_back(chat_tool(std::move(write_function)));
    tools.emplace_back(chat_tool(std::move(edit_function)));
    tools.emplace_back(chat_tool(std::move(delete_function)));
  }

  if (capabilities.online()) {
    tools.emplace_back(chat_tool(std::move(fetch_function)));
    tools.emplace_back(chat_tool(std::move(web_search_function)));
  }

  if (capabilities.terminal()) {
    tools.emplace_back(chat_tool(std::move(run_function)));
  }

  return tools;
}

boost::json::array felis::bell::responses_tools(const felis::bell::tool_capabilities& capabilities) {
  boost::json::array tools;

  for (const boost::json::value& value: tool_definitions(capabilities)) {
    const boost::json::object& tool = value.as_object();
    const boost::json::object& function = tool.at("function").as_object();

    // Responses flattens the schema onto the tool itself instead of nesting it
    // under `function`.
    boost::json::object flattened;
    flattened["type"] = "function";
    flattened["name"] = function.at("name");
    flattened["description"] = function.at("description");
    flattened["parameters"] = function.at("parameters");

    tools.emplace_back(std::move(flattened));
  }

  return tools;
}
