#include "html_sanitizer.h"

#include "logging.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace onerss::backend {
namespace {

const std::unordered_set<std::string> kAllowedTags{
  "p", "br", "b", "strong", "i", "em", "u", "code", "pre", "blockquote", "ul", "ol", "li", "a",
};

const std::unordered_set<std::string> kTransparentTags{
  "html", "title", "body",
};

const std::unordered_set<std::string> kDropContentTags{
  "script", "style",
};

std::string toLower(std::string_view input) {
  std::string result;
  result.reserve(input.size());
  for (const auto ch : input) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return result;
}

std::string trim(std::string_view input) {
  std::size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  std::size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return std::string{input.substr(start, end - start)};
}

std::size_t findTagEnd(const std::string &input, const std::size_t start) {
  bool in_single_quote = false;
  bool in_double_quote = false;
  for (std::size_t i = start; i < input.size(); ++i) {
    const auto ch = input[i];
    if (ch == '\'' && !in_double_quote) {
      in_single_quote = !in_single_quote;
    } else if (ch == '"' && !in_single_quote) {
      in_double_quote = !in_double_quote;
    } else if (ch == '>' && !in_single_quote && !in_double_quote) {
      return i;
    }
  }
  return std::string::npos;
}

struct ParsedTag {
  std::string name;
  std::string attributes;
  bool closing = false;
  bool self_closing = false;
};

std::optional<ParsedTag> parseTag(std::string_view raw_tag) {
  auto trimmed = trim(raw_tag);
  std::string_view raw = trimmed;
  if (raw.empty() || raw.front() == '!' || raw.front() == '?') {
    return std::nullopt;
  }

  ParsedTag parsed;
  if (raw.front() == '/') {
    parsed.closing = true;
    raw.remove_prefix(1);
    trimmed = trim(raw);
    raw = trimmed;
  }

  if (raw.empty()) {
    return std::nullopt;
  }

  std::size_t name_end = 0;
  while (name_end < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[name_end])) || raw[name_end] == '-')) {
    ++name_end;
  }
  if (name_end == 0) {
    return std::nullopt;
  }

  parsed.name = toLower(raw.substr(0, name_end));
  auto tail = trim(raw.substr(name_end));
  if (!tail.empty() && tail.back() == '/') {
    parsed.self_closing = true;
    tail.pop_back();
    tail = trim(tail);
  }
  parsed.attributes = tail;
  return parsed;
}

std::optional<std::string> extractHref(std::string_view attributes) {
  const auto lowered = toLower(attributes);
  std::size_t pos = 0;
  while ((pos = lowered.find("href", pos)) != std::string::npos) {
    const auto before_ok = pos == 0 || !std::isalnum(static_cast<unsigned char>(lowered[pos - 1]));
    const auto after_index = pos + 4;
    const auto after_ok = after_index >= lowered.size() || !std::isalnum(static_cast<unsigned char>(lowered[after_index]));
    if (!before_ok || !after_ok) {
      pos = after_index;
      continue;
    }

    std::size_t i = after_index;
    while (i < attributes.size() && std::isspace(static_cast<unsigned char>(attributes[i]))) {
      ++i;
    }
    if (i >= attributes.size() || attributes[i] != '=') {
      pos = after_index;
      continue;
    }
    ++i;
    while (i < attributes.size() && std::isspace(static_cast<unsigned char>(attributes[i]))) {
      ++i;
    }
    if (i >= attributes.size()) {
      return std::nullopt;
    }

    std::string value;
    if (attributes[i] == '"' || attributes[i] == '\'') {
      const auto quote = attributes[i++];
      const auto start = i;
      while (i < attributes.size() && attributes[i] != quote) {
        ++i;
      }
      value = std::string{attributes.substr(start, i - start)};
    } else {
      const auto start = i;
      while (i < attributes.size() && !std::isspace(static_cast<unsigned char>(attributes[i]))) {
        ++i;
      }
      value = std::string{attributes.substr(start, i - start)};
    }
    return trim(value);
  }

  return std::nullopt;
}

bool isSafeHref(std::string_view href) {
  if (href.empty()) {
    return false;
  }
  const auto lowered = toLower(trim(href));
  if (lowered.find("javascript:") != std::string::npos || lowered.find("vbscript:") != std::string::npos
      || lowered.find("data:") != std::string::npos || lowered.find("script") != std::string::npos
      || lowered.find("onload") != std::string::npos || lowered.find("onclick") != std::string::npos) {
    return false;
  }

  return lowered.starts_with("http://") || lowered.starts_with("https://") || lowered.starts_with("mailto:")
         || lowered.starts_with("/") || lowered.starts_with("#") || lowered.starts_with("./")
         || lowered.starts_with("../");
}

std::size_t skipTagContent(const std::string &input, const std::size_t content_start, const std::string &tag_name) {
  const auto close_tag = "</" + tag_name;
  const auto lowered = toLower(std::string_view{input}.substr(content_start));
  const auto pos = lowered.find(close_tag);
  if (pos == std::string::npos) {
    return input.size();
  }
  const auto absolute = content_start + pos;
  const auto end = findTagEnd(input, absolute + 1);
  return end == std::string::npos ? input.size() : end + 1;
}

std::string escapeAttribute(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const auto ch : input) {
    switch (ch) {
      case '&':
        output.append("&amp;");
        break;
      case '"':
        output.append("&quot;");
        break;
      case '<':
        output.append("&lt;");
        break;
      case '>':
        output.append("&gt;");
        break;
      default:
        output.push_back(ch);
        break;
    }
  }
  return output;
}

}  // namespace

std::string sanitizePreviewHtml(const std::string &input) {
  std::string output;
  output.reserve(input.size());

  for (std::size_t i = 0; i < input.size();) {
    if (input[i] != '<') {
      output.push_back(input[i]);
      ++i;
      continue;
    }

    if (i + 3 < input.size() && input.compare(i, 4, "<!--") == 0) {
      const auto comment_end = input.find("-->", i + 4);
      i = comment_end == std::string::npos ? input.size() : comment_end + 3;
      continue;
    }

    const auto tag_end = findTagEnd(input, i + 1);
    if (tag_end == std::string::npos) {
      output.append(input.substr(i));
      break;
    }

    const auto parsed = parseTag(std::string_view{input}.substr(i + 1, tag_end - i - 1));
    if (!parsed.has_value()) {
      i = tag_end + 1;
      continue;
    }

    const auto &tag = *parsed;
    if (kDropContentTags.contains(tag.name) && !tag.closing) {
      LOG_TRACE << "Dropping disallowed tag with content: " << tag.name;
      i = skipTagContent(input, tag_end + 1, tag.name);
      continue;
    }

    if (kTransparentTags.contains(tag.name)) {
      i = tag_end + 1;
      continue;
    }

    if (!kAllowedTags.contains(tag.name)) {
      i = tag_end + 1;
      continue;
    }

    if (tag.closing) {
      if (tag.name != "br") {
        output.append("</");
        output.append(tag.name);
        output.push_back('>');
      }
      i = tag_end + 1;
      continue;
    }

    output.push_back('<');
    output.append(tag.name);
    if (tag.name == "a") {
      if (const auto href = extractHref(tag.attributes); href.has_value() && isSafeHref(*href)) {
        output.append(" href=\"");
        output.append(escapeAttribute(*href));
        output.push_back('"');
      }
    }
    if (tag.name == "br" || tag.self_closing) {
      output.append("/>");
    } else {
      output.push_back('>');
    }
    i = tag_end + 1;
  }

  return output;
}

}  // namespace onerss::backend
