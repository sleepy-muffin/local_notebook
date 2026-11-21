
#include <string>
#include <stop_token>

#include <felis/str.hpp>
#include <felis/net/http.hpp>

#include <boost/json.hpp>
#include <boost/url.hpp>
#include <boost/regex.hpp>

#include "web_tools.hpp"

namespace {

struct HttpResponse {
  int status = 0;
  std::string body;
};

struct SearchResult {
  std::string title;
  std::string url;
  std::string snippet;
};

struct SearchOutcome {
  std::vector<SearchResult> results;
  std::string note;
  std::string message;
};

void append_utf8(std::string& out, char32_t code) {
  if (code <= 0x7F) {
    out.push_back(static_cast<char>(code));
  } else if (code <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

std::string trim(std::string_view text) {
  const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
  const auto begin = std::find_if(text.begin(), text.end(), not_space);
  const auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();

  if (begin >= end) {
    return {};
  }

  return std::string(begin, end);
}

bool is_loopback_url(const std::string& url) {
  const auto parsed = boost::urls::parse_uri(url);

  if (!parsed) {
    return false;
  }

  std::string host(parsed->host());

  for (char& c: host) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  return host == "127.0.0.1" || host == "localhost" || host == "::1" ||
      host == "[::1]" || host == "0.0.0.0";
}


bool equals_ignore_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }

  return true;
}

// Trims every line and collapses runs of blank lines into a single one. Pages
// are full of markup-only lines that leave nothing but whitespace behind once
// the tags are stripped, so without this the fetched text arrives as a sparse
// column of empty rows.
std::string tidy_lines(const std::string& text) {
  std::string out;
  out.reserve(text.size());

  // Set when a blank line was seen, so it is written back only if another line
  // of text follows. This also drops blank lines at the end.
  bool blank_pending = false;

  for (std::size_t begin = 0; begin < text.size();) {
    std::size_t end = text.find('\n', begin);

    if (end == std::string::npos) {
      end = text.size();
    }

    const std::string line = trim(std::string_view(text).substr(begin, end - begin));

    if (line.empty()) {
      blank_pending = !out.empty();
    } else {
      if (blank_pending) {
        out.push_back('\n');
        blank_pending = false;
      }

      out += line;
      out.push_back('\n');
    }

    begin = end + 1;
  }

  // The loop leaves a trailing newline; `trim` removes it along with any other
  // leading or trailing whitespace.
  return trim(out);
}



void replace_all(std::string& text, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return;
  }

  std::size_t at = 0;

  while ((at = text.find(from, at)) != std::string::npos) {
    text.replace(at, from.size(), to);
    at += to.size();
  }
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

std::string searxng_error_message(const std::string& body, int status) {
  boost::system::error_code error;
  const boost::json::value parsed = boost::json::parse(body, error);

  if (!error && parsed.is_object()) {
    const boost::json::object& object = parsed.as_object();
    const std::string message = json_string_or(object, "error");
    const boost::json::value* challenge_value = object.if_contains("challenge");
    const bool challenge = challenge_value != nullptr &&
        challenge_value->is_bool() && challenge_value->as_bool();

    if (!message.empty()) {
      if (challenge) {
        return message +
            " Clear the search service's verification check, then retry.";
      }

      return message;
    }
  }

  return "HTTP " + std::to_string(status);
}


std::string url_encode(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";

  std::string out;
  out.reserve(text.size() * 3);

  for (unsigned char c: text) {
    if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }

  return out;
}

// A search API answers JSON, not HTML.
const std::vector<felis::net::HttpHeader> kJsonHeaders{
    {"Accept", "application/json"},
};


HttpResponse http_get_all(
    const std::string& url, const std::optional<std::string>& proxy, int max_bytes,
    const std::vector<felis::net::HttpHeader>& headers, int read_timeout_ms,
    std::stop_token stop
) {
  std::vector<felis::net::HttpHeader> merged{
      {"User-Agent", "Mozilla/5.0 (compatible; local-notebook/1.0)"},
  };

  // A caller-supplied header replaces the default of the same name.
  for (const felis::net::HttpHeader& extra: headers) {
    bool replaced = false;

    for (felis::net::HttpHeader& header: merged) {
      if (equals_ignore_case(header.name, extra.name)) {
        header = extra;
        replaced = true;
        break;
      }
    }

    if (!replaced) {
      merged.push_back(extra);
    }
  }

  HttpResponse response;
  const std::size_t limit =
      max_bytes > 0 ? static_cast<std::size_t>(max_bytes) : 0;

  const felis::net::ResponseHandler on_response =
      [&](int status, const std::vector<felis::net::HttpHeader>&) {
        response.status = status;
        return true;
      };

  const felis::net::BodyHandler on_body = [&](std::string_view chunk) {
    if (response.body.size() < limit) {
      response.body.append(chunk.substr(0, limit - response.body.size()));
    }

    return true;
  };

  http_request(
      "GET", url, proxy, merged, "", stop, on_response, on_body,
      30'000, 5, read_timeout_ms
  );

  return response;
}

// ---------------------------------------------------------------------
//
// ---------------------------------------------------------------------
}

std::string decode_entities(const std::string& input) {
  std::string text = input;
  replace_all(text, "&nbsp;", " ");
  replace_all(text, "&lt;", "<");
  replace_all(text, "&gt;", ">");
  replace_all(text, "&quot;", "\"");
  replace_all(text, "&#39;", "'");
  replace_all(text, "&apos;", "'");

  static const boost::regex numeric(
      "&#(x?[0-9A-Fa-f]+);", boost::regex_constants::perl
  );

  std::string decoded;
  decoded.reserve(text.size());

  std::size_t last = 0;

  for (boost::sregex_iterator it(text.begin(), text.end(), numeric), end;
       it != end; ++it) {
    const boost::smatch& match = *it;
    const std::string body = match[1].str();

    bool ok = false;
    unsigned long code = 0;

    try {
      std::size_t consumed = 0;

      if (!body.empty() && (body.front() == 'x' || body.front() == 'X')) {
        code = std::stoul(body.substr(1), &consumed, 16);
        ok = consumed == body.size() - 1;
      } else {
        code = std::stoul(body, &consumed, 10);
        ok = consumed == body.size();
      }
    } catch (const std::exception&) {
      ok = false;
    }

    if (!ok || code == 0 || code > 0x10FFFF) {
      continue;
    }

    decoded.append(text, last, static_cast<std::size_t>(match.position()) - last);
    append_utf8(decoded, static_cast<char32_t>(code));
    last = static_cast<std::size_t>(match.position() + match.length());
  }

  decoded.append(text, last, std::string::npos);
  replace_all(decoded, "&amp;", "&");

  return decoded;
}


// Reduces an HTML fragment to readable text: drops script/style bodies, strips
// the remaining tags and decodes entities.
std::string html_to_text(const std::string& html) {
  static const boost::regex script(
      "<script\\b[^>]*>.*?</script>",
      boost::regex_constants::perl | boost::regex_constants::icase |
          boost::regex_constants::mod_s
  );
  static const boost::regex style(
      "<style\\b[^>]*>.*?</style>",
      boost::regex_constants::perl | boost::regex_constants::icase |
          boost::regex_constants::mod_s
  );
  static const boost::regex tag("<[^>]*>", boost::regex_constants::perl);
  // Stripping adjacent tags leaves runs of spaces ("<b>x</b> y" becomes
  // " x  y"); collapse those without touching newlines, which carry the
  // paragraph structure of a fetched page.
  static const boost::regex space_run(
      "[ \\t]{2,}", boost::regex_constants::perl
  );

  std::string text = boost::regex_replace(html, script, " ");
  text = boost::regex_replace(text, style, " ");
  text = boost::regex_replace(text, tag, " ");
  text = boost::regex_replace(text, space_run, " ");

  return tidy_lines(decode_entities(text));
}

// Public search and content endpoints reject non-browser clients: DuckDuckGo
// serves its bot-challenge page to the plain `local-notebook` User-Agent that
// `http_get_body` sends by default. These headers make the request look like an
// ordinary page load, which is what keeps the HTML result pages readable.
const std::vector<felis::net::HttpHeader> kBrowserHeaders{
    {"User-Agent",
     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
     "Chrome/124.0 Safari/537.36"},
    {"Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
    {"Accept-Language", "en-US,en;q=0.9"},
};

constexpr int kFetchBodyLimit = 512 * 1024; // bytes read from one response
constexpr int kFetchTextLimit = 8000; // characters returned to the model

std::string http_get_with(
    const std::string& url, const std::optional<std::string>& proxy, int max_bytes,
    const std::vector<felis::net::HttpHeader>& headers, std::stop_token stop
) {
  auto body = http_get_body(url, proxy, max_bytes, stop, headers);

  if (!body) {
    throw std::runtime_error(
        "HTTP request failed (HTTP " + std::to_string(body.error()) + ")"
    );
  }

  return std::move(*body);
}

std::string http_get(
    const std::string& url, const std::optional<std::string>& proxy, int max_bytes,
    std::stop_token stop
) {
  return http_get_with(url, proxy, max_bytes, kBrowserHeaders, stop);
}

std::string felis::bell::fetch_url(
    const std::string& url, const std::optional<std::string>& proxy,
    std::stop_token stop
) {
  const std::string trimmed_url = felis::str::trim(url);

  if (trimmed_url.empty()) {
    throw std::runtime_error("A URL is required");
  }

  // A page is bytes, not text: one served as GBK (or any other encoding) would
  // otherwise carry sequences that are not valid UTF-8 into the request body,
  // which a strict JSON server rejects outright. The same applies to the cut
  // below, which must land on a code-point boundary rather than in the middle
  // of a multi-byte character.
  const std::string text = felis::str::sanitize_utf8(
      html_to_text(http_get(trimmed_url, proxy, kFetchBodyLimit, stop))
  );

  if (text.empty()) {
    return "The page returned no readable text.";
  }

  if (text.size() > static_cast<std::size_t>(kFetchTextLimit)) {
    return felis::str::truncate_utf8(text, kFetchTextLimit) +
        "\n\n[Page truncated; only the first part is shown.]";
  }

  return text;
}


constexpr std::size_t kMaxSearchResults = 8;


// Renders parsed hits in the "Title / URL / snippet" shape the model expects.
std::string format_search_results(const std::vector<SearchResult>& results) {
  std::ostringstream out;
  std::size_t count = 0;

  for (const SearchResult& result: results) {
    if (result.title.empty() || result.url.empty()) {
      continue;
    }

    ++count;
    out << count << ". " << result.title << '\n';
    out << result.url << '\n';

    if (!result.snippet.empty()) {
      out << result.snippet << '\n';
    }

    out << '\n';

    if (count == kMaxSearchResults) {
      break;
    }
  }

  return out.str();
}

std::vector<SearchResult> parse_searxng_results(const std::string& json) {
  std::vector<SearchResult> out;

  boost::system::error_code error;
  const boost::json::value parsed = boost::json::parse(json, error);

  if (error || !parsed.is_object()) {
    return out;
  }

  const boost::json::value* results = parsed.as_object().if_contains("results");

  if (results == nullptr || !results->is_array()) {
    return out;
  }

  for (const boost::json::value& entry: results->as_array()) {
    if (!entry.is_object()) {
      continue;
    }

    const boost::json::object& object = entry.as_object();
    SearchResult result;
    result.title = json_string_or(object, "title");
    result.url = json_string_or(object, "url");
    result.snippet = json_string_or(object, "content");

    if (result.title.empty() || result.url.empty()) {
      continue;
    }

    out.push_back(std::move(result));
  }

  return out;
}

SearchOutcome search_searxng(
    const std::string& endpoint, const std::string& query,
    const std::optional<std::string>& proxy, std::stop_token stop
) {
  std::string base = trim(endpoint);

  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }

  // A loopback endpoint must bypass the proxy, which would otherwise intercept
  // the request to this machine.
  const std::optional<std::string> effective =
      is_loopback_url(base) ? std::nullopt : proxy;

  const std::string url = base + "/search?q=" + url_encode(query) + "&format=json";
  const HttpResponse response =
      http_get_all(url, effective, kFetchBodyLimit, kJsonHeaders, 180'000, stop);

  if (response.status < 200 || response.status >= 300) {
    throw std::runtime_error(searxng_error_message(response.body, response.status));
  }

  SearchOutcome outcome;
  outcome.results = parse_searxng_results(response.body);

  if (outcome.results.empty()) {
    outcome.message = "no results";
    return outcome;
  }

  // A cached answer is slightly outdated but stable; saying so lets the model
  // treat a repeated question as converging instead of contradicting itself.
  boost::system::error_code error;
  const boost::json::value parsed = boost::json::parse(response.body, error);

  if (!error && parsed.is_object()) {
    const boost::json::value* lnb = parsed.as_object().if_contains("lnb");

    if (lnb != nullptr && lnb->is_object()) {
      const boost::json::object& note = lnb->as_object();
      const boost::json::value* stale_value = note.if_contains("stale");
      const bool stale = stale_value != nullptr && stale_value->is_bool() &&
          stale_value->as_bool();

      if (stale) {
        const int days = json_int_or(note, "age_days", 0);
        outcome.note =
            "[These results came from the local search cache, about " +
            std::to_string(days) + " day(s) old; the engines could not be "
            "reached.]";
      }
    }
  }

  return outcome;
}


std::string felis::bell::web_search(
    const std::string& query, const std::optional<std::string>& proxy,
    const std::string& endpoint, std::stop_token stop
) {
  const std::string trimmed_query = trim(query);

  if (trimmed_query.empty()) {
    throw std::runtime_error("A search query is required");
  }

  // With no endpoint configured there is nowhere to send the query. Report it as
  // a plain message rather than an exception, so the model can ask the user to
  // set one instead of treating the tool as broken.
  if (trim(endpoint).empty()) {
    return "Web search is not configured. Ask the user to set the assistant's "
        "Search endpoint to a running SearXNG-compatible service, then retry.";
  }

  try {
    const SearchOutcome outcome =
        search_searxng(endpoint, trimmed_query, proxy, stop);

    if (!outcome.results.empty()) {
      std::string text = format_search_results(outcome.results);

      if (!outcome.note.empty()) {
        text = outcome.note + "\n\n" + text;
      }

      return text;
    }

    return "Web search returned " +
        (outcome.message.empty() ? std::string("no results") : outcome.message) +
        ". Try fetch_url on a known URL, or rephrase the query.";
  } catch (const std::exception& error) {
    // A cancelled request is not a search failure: let the caller report the
    // cancellation rather than dressing it up as a broken endpoint.
    if (stop.stop_requested()) {
      throw;
    }

    return "Web search failed (" + std::string(error.what()) +
        "). Try fetch_url on a known URL, or check the assistant's Search "
        "endpoint setting.";
  }
}
