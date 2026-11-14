
#include <string>
#include <filesystem>
#include <vector>

#include <boost/json.hpp>
#include <boost/regex.hpp>

#include <felis/str.hpp>
#include <felis/file/file.hpp>
#include <felis/file/search.hpp>

#include <felis/net/llm.hpp>

#include "tools.hpp"
#include "message.hpp"
#include "web_tools.hpp"


namespace {

// One configured directory. `name` is the only identifier the model sees; the
// actual path is never included in tool output.
struct SharedRoot {
  std::string name;
  std::filesystem::path path;
  bool available = false;
};

// Builds the synthetic tree's top level. A single directory is exposed at the
// root, so its visible name is unused; several are exposed as named children.
std::vector<SharedRoot> build_shared_roots(
    const std::vector<std::string>& directories
) {
  std::vector<SharedRoot> roots;

  for (const std::string& directory: directories) {
    if (directory.empty()) continue;

    const std::filesystem::path configured(directory);
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::canonical(configured, error);
    std::error_code type_error;
    const bool available = !error &&
        std::filesystem::is_directory(canonical, type_error) && !type_error;

    SharedRoot root;
    root.path = available ? canonical : configured;
    root.available = available;

    // The visible name is the directory's own name, never the path leading to
    // it. Duplicates get a stable suffix so each mount is addressable.
    std::string name =
        (available ? canonical : configured).filename().generic_string();

    if (name.empty() || name == "." || name == "..") {
      name = "root";
    }

    std::string unique = name;

    for (int suffix = 2;; ++suffix) {
      const bool taken = std::any_of(
          roots.begin(), roots.end(),
          [&unique](const SharedRoot& root) { return root.name == unique; }
      );

      if (!taken) break;

      unique = name + "-" + std::to_string(suffix);
    }

    root.name = std::move(unique);
    roots.push_back(std::move(root));
  }

  return roots;
}

// A resolved model path. `relative` is internal to the selected root and
// `display` is the equivalent path in the model-visible tree.
struct PathTarget {
  bool all_roots = false;
  const SharedRoot* root = nullptr;
  std::string relative;
  std::string display;
};

PathTarget resolve_target(
    const std::vector<SharedRoot>& roots, const std::string& requested,
    bool allow_all_roots
) {
  const std::filesystem::path input(requested.empty() ? "." : requested);

  if (input.is_absolute()) {
    throw std::runtime_error("Paths must be relative to the shared directories");
  }

  std::string relative = input.lexically_normal().generic_string();

  if (relative == ".") {
    relative.clear();
  }

  if (relative == ".." || relative.rfind("../", 0) == 0) {
    throw std::runtime_error("Path is outside the shared directories");
  }

  PathTarget target;

  if (roots.size() == 1) {
    target.root = &roots.front();
    target.relative = relative;
    target.display = relative;
  } else if (relative.empty()) {
    if (!allow_all_roots) {
      throw std::runtime_error(
          "A specific path is required when several shared directories are "
          "configured"
      );
    }

    target.all_roots = true;
    target.display = ".";
  } else {
    const std::size_t slash = relative.find('/');
    const std::string mounted = relative.substr(0, slash);

    const auto found = std::find_if(
        roots.begin(), roots.end(),
        [&mounted](const SharedRoot& root) { return root.name == mounted; }
    );

    if (found == roots.end()) {
      throw std::runtime_error(
          "Unknown shared directory \"" + mounted +
          "\". Call list_directory with no path to see the available "
          "directories."
      );
    }

    target.root = &*found;
    target.relative = slash == std::string::npos
        ? std::string{}
        : relative.substr(slash + 1);
    target.display = relative;
  }

  if (target.root != nullptr && !target.root->available) {
    if (roots.size() == 1) {
      throw std::runtime_error("The shared directory is unavailable");
    }

    throw std::runtime_error(
        "The shared directory \"" + target.root->name + "\" is unavailable"
    );
  }

  return target;
}

struct SearchSegment {
  const SharedRoot* root;
  std::string relative;
  std::string prefix;
};

struct SearchPlan {
  std::vector<SearchSegment> segments;
  std::string scope; // model-visible description, never an actual path
};

SearchPlan plan_search(
    const std::vector<SharedRoot>& roots, const std::string& requested
) {
  const PathTarget target = resolve_target(roots, requested, true);

  SearchPlan plan;

  if (target.all_roots) {
    plan.scope = "the configured shared directories";

    for (const SharedRoot& root: roots) {
      if (root.available) {
        plan.segments.push_back({&root, {}, root.name});
      }
    }

    if (plan.segments.empty()) {
      throw std::runtime_error("No shared directories are currently accessible");
    }
  } else {
    plan.scope = target.display.empty()
        ? "the shared directory"
        : "\"" + target.display + "\"";
    plan.segments.push_back(
        {target.root, target.relative,
         roots.size() > 1 ? target.root->name : std::string{}}
    );
  }

  return plan;
}

std::string run_search_code(
    const std::vector<SharedRoot>& roots, const std::string& query,
    const std::string& requested
) {
  const SearchPlan plan = plan_search(roots, requested);
  constexpr int kMaxMatches = 30;

  std::string text;
  int matches = 0;
  int files_scanned = 0;
  bool truncated = false;

  for (const SearchSegment& segment: plan.segments) {
    const felis::file::TextSearchResult result = felis::file::search_plain(
        query, segment.root->path.string(), segment.relative, segment.prefix
    );

    text += result.text;
    matches += result.matches;
    files_scanned += result.files_scanned;
    truncated = truncated || result.truncated;

    if (matches >= kMaxMatches) {
      truncated = true;
      break;
    }
  }

  if (matches == 0) {
    return "No matches for \"" + query + "\" in " + plan.scope + " (" +
        std::to_string(files_scanned) +
        " text files scanned). Binary files are skipped; try a different term "
        "or a broader path.";
  }

  if (truncated) {
    text += "\n[Result limit reached; narrow the search path or query.]\n";
  }

  return text;
}

std::string run_search_regex(
    const std::vector<SharedRoot>& roots, const std::string& pattern,
    const std::string& requested, const std::string& glob
) {
  const SearchPlan plan = plan_search(roots, requested);
  constexpr int kMaxMatches = 30;

  std::string text;
  int matches = 0;
  int files_scanned = 0;
  bool truncated = false;

  for (const SearchSegment& segment: plan.segments) {
    const felis::file::TextSearchResult result = felis::file::search_regex(
        pattern, segment.root->path.string(), segment.relative, glob,
        segment.prefix
    );

    text += result.text;
    matches += result.matches;
    files_scanned += result.files_scanned;
    truncated = truncated || result.truncated;

    if (matches >= kMaxMatches) {
      truncated = true;
      break;
    }
  }

  if (matches == 0) {
    return "No matches for \"" + pattern + "\" in " + plan.scope + " (" +
        std::to_string(files_scanned) +
        " text files scanned). Binary files and ignored paths are skipped.";
  }

  if (truncated) {
    text +=
        "\n[Result limit reached; narrow the search path, glob or pattern.]\n";
  }

  return text;
}

std::string run_find_files(
    const std::vector<SharedRoot>& roots, const std::string& glob,
    const std::string& requested
) {
  const SearchPlan plan = plan_search(roots, requested);
  constexpr std::size_t kMaxListedFiles = 1000;

  std::vector<std::string> paths;
  std::size_t files_scanned = 0;
  bool truncated = false;

  for (const SearchSegment& segment: plan.segments) {
    felis::file::FileListResult result = felis::file::find_files(
        glob, segment.root->path.string(), segment.relative, segment.prefix
    );

    files_scanned += result.scanned;
    truncated = truncated || result.truncated;
    paths.insert(
        paths.end(), std::make_move_iterator(result.paths.begin()),
        std::make_move_iterator(result.paths.end())
    );
  }

  std::sort(paths.begin(), paths.end());

  if (paths.size() > kMaxListedFiles) {
    paths.resize(kMaxListedFiles);
    truncated = true;
  }

  if (paths.empty()) {
    return "No files match \"" + glob + "\" in " + plan.scope + " (" +
        std::to_string(files_scanned) +
        " files scanned). Hidden and ignored paths are skipped.";
  }

  std::ostringstream out;

  for (const std::string& path: paths) {
    out << path << '\n';
  }

  if (truncated) {
    out << "[File limit reached; narrow the path or glob.]\n";
  }

  return out.str();
}

bool is_top_level_request(const std::string& requested) {
  if (requested.empty()) return true;

  const std::filesystem::path input(requested);

  if (input.is_absolute()) return false;

  const std::string normalized = input.lexically_normal().generic_string();
  return normalized.empty() || normalized == ".";
}

std::string run_list_directory(
    const std::vector<SharedRoot>& roots, const std::string& requested
) {
  // With several directories, the top level is the list of mounts. The model
  // picks one by name; a single directory is transparent and its contents are
  // listed directly.
  if (roots.size() > 1 && is_top_level_request(requested)) {
    std::vector<std::string> entries;

    for (const SharedRoot& root: roots) {
      if (root.available) {
        entries.push_back(root.name + "/");
      }
    }

    if (entries.empty()) {
      return "No shared directories are currently accessible.";
    }

    std::sort(entries.begin(), entries.end());

    std::ostringstream out;

    for (const std::string& entry: entries) {
      out << entry << '\n';
    }

    return out.str();
  }

  const PathTarget target = resolve_target(roots, requested, false);
  return felis::file::list_directory(target.root->path.string(), target.relative);
}

std::string json_string_or(
    const boost::json::object& object, std::string_view key, std::string fallback = {}
) {
  const boost::json::value* value = object.if_contains(key);

  return value != nullptr && value->is_string()
      ? std::string(value->as_string())
      : std::move(fallback);
}

int json_int_or(
    const boost::json::object& object, std::string_view key, int fallback
) {
  const boost::json::value* value = object.if_contains(key);

  if (value == nullptr || !value->is_number()) {
    return fallback;
  }

  if (value->is_int64()) {
    return static_cast<int>(value->as_int64());
  }

  if (value->is_uint64()) {
    return static_cast<int>(value->as_uint64());
  }

  return static_cast<int>(value->as_double());
}



// ========== ---------- ---------- ---------- ---------- ---------- ==========
//                          END OF ANONYMOUS NAMESPACE
// ========== ---------- ---------- ---------- ---------- ---------- ==========
} // namespace

std::string felis::bell::execute_local_tool(
    const felis::net::llm::ToolCall& call, const std::vector<std::string>& shared_directories,
    const std::optional<std::string>& proxy, const felis::bell::tool_capabilities& permissions
) {
  try {
    boost::system::error_code parse_error;
    const boost::json::value parsed =
        boost::json::parse(call.arguments, parse_error);

    if (parse_error || !parsed.is_object()) {
      throw std::runtime_error("Tool arguments must be a JSON object");
    }

    const boost::json::object& args = parsed.as_object();

    if (permissions.empty()) {
      return "Tool error: tools are disabled for this session";
    }

    const bool is_read_tool = call.name == "list_directory" ||
        call.name == "search_code" || call.name == "search_code_regex" ||
        call.name == "find_files" || call.name == "read_file";
    const bool is_write_tool = call.name == "write_file" ||
        call.name == "edit_file" || call.name == "delete_file";
    const bool is_online_tool =
        call.name == "fetch_url" || call.name == "web_search";

    if (is_read_tool && !permissions.repo_read()) {
      return "Tool error: file reading is disabled for this session";
    }

    if (is_write_tool && !permissions.repo_write()) {
      return "Tool error: file writing is disabled for this session";
    }

    if (is_online_tool && !permissions.online()) {
      return "Tool error: online access is disabled for this session";
    }

    // Filesystem tools need at least one shared directory; network tools do
    // not, so the gate lives here instead of around the whole call.
    const bool needs_root = is_read_tool || is_write_tool;

    const std::vector<SharedRoot> roots = needs_root
        ? build_shared_roots(shared_directories)
        : std::vector<SharedRoot>{};

    if (needs_root && roots.empty()) {
      return "Tool error: this session has no shared directories configured";
    }

    if (call.name == "list_directory") {
      return run_list_directory(roots, json_string_or(args, "path", "."));
    }

    if (call.name == "search_code") {
      const std::string query = json_string_or(args, "query");
      return run_search_code(roots, query, json_string_or(args, "path", "."));
    }

    if (call.name == "search_code_regex") {
      const std::string pattern = json_string_or(args, "pattern");
      const std::string glob = json_string_or(args, "glob");
      return run_search_regex(
          roots, pattern, json_string_or(args, "path", "."), glob
      );
    }

    if (call.name == "find_files") {
      const std::string glob = json_string_or(args, "glob");
      return run_find_files(roots, glob, json_string_or(args, "path", "."));
    }

    if (call.name == "read_file") {
      const std::string path = json_string_or(args, "path");
      const int start = json_int_or(args, "start_line", 1);
      const int end = json_int_or(args, "end_line", start + 100);

      const PathTarget target = resolve_target(roots, path, false);
      return felis::file::read_file_range(
          felis::file::checked_repo_path(target.root->path.string(), target.relative), start,
          end
      );
    }

    if (call.name == "write_file") {
      const std::string path = json_string_or(args, "path");
      const boost::json::value* content_value = args.if_contains("content");

      if (content_value == nullptr || !content_value->is_string()) {
        throw std::runtime_error("Missing content");
      }

      const std::string content(content_value->as_string());
      const PathTarget target = resolve_target(roots, path, false);
      const std::filesystem::path file = felis::file::checked_repo_path_for_write(
          target.root->path.string(), target.relative
      );

      felis::file::write_file_contents(file, content);
      return "Wrote " + std::to_string(content.size()) + " bytes to " +
          target.display + ".";
    }

    if (call.name == "edit_file") {
      const std::string path = json_string_or(args, "path");
      const boost::json::value* old_value = args.if_contains("old_text");
      const boost::json::value* new_value = args.if_contains("new_text");

      if (old_value == nullptr || !old_value->is_string() || new_value == nullptr ||
          !new_value->is_string()) {
        throw std::runtime_error("Missing old_text or new_text");
      }

      const std::string old_text(old_value->as_string());
      const std::string new_text(new_value->as_string());

      if (old_text.empty()) {
        throw std::runtime_error("old_text must not be empty");
      }

      const PathTarget target = resolve_target(roots, path, false);
      const std::filesystem::path file = felis::file::checked_repo_path(
          target.root->path.string(), target.relative
      );
      std::string content = felis::file::read_whole_file(file);
      const std::size_t first = content.find(old_text);

      if (first == std::string::npos) {
        throw std::runtime_error("The text to replace was not found");
      }

      if (content.find(old_text, first + old_text.size()) != std::string::npos) {
        throw std::runtime_error(
            "The text to replace occurs more than once; include more context"
        );
      }

      const auto line = 1 + std::count(
          content.begin(), content.begin() + static_cast<std::ptrdiff_t>(first),
          '\n'
      );

      content.replace(first, old_text.size(), new_text);
      felis::file::write_file_contents(file, content);

      return "Replaced one occurrence at line " + std::to_string(line) +
          " in " + target.display + ".";
    }

    if (call.name == "delete_file") {
      const std::string path = json_string_or(args, "path");
      const PathTarget target = resolve_target(roots, path, false);
      const std::filesystem::path file = felis::file::checked_repo_path_for_delete(
          target.root->path.string(), target.relative
      );

      std::error_code error;
      if (!std::filesystem::remove(file, error) || error) {
        throw std::runtime_error("Cannot remove file");
      }

      return "Deleted " + target.display + ".";
    }

    if (call.name == "fetch_url") {
      const std::string url = json_string_or(args, "url");
      return felis::bell::fetch_url(url, proxy);
    }

    // if (call.name == "web_search") {
    //   const std::string query = json_string_or(args, "query");
    //   return felis::bell::web_search(query, proxy, permissions.online());
    // }

    throw std::runtime_error("Unknown tool: " + call.name);
  } catch (const std::filesystem::filesystem_error&) {
    // A filesystem exception carries an actual path in its message, so it is
    // deliberately replaced with a path-free message before reaching the model.
    return "Tool error: The path could not be accessed";
  } catch (const std::exception& e) {
    // Tool errors are returned to the model so it can retry with better
    // arguments instead of aborting the whole generation.
    return "Tool error: " + std::string(e.what());
  }
}
