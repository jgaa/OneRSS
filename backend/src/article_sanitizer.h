#pragma once

#include "article_types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace onerss::backend {

struct ArticleSanitizationResult {
  bool accepted = false;
  bool modified = false;
  bool content_capped = false;
  bool content_control_bytes_normalized = false;
  std::string rejection_reason;
  ArticleRecord article;
};

[[nodiscard]] ArticleSanitizationResult sanitizeArticleForStorage(ArticleRecord article);
[[nodiscard]] std::size_t maxStoredArticleContentBytes() noexcept;

}  // namespace onerss::backend

namespace onerss::backend {
namespace detail {

inline constexpr std::size_t kMaxGuidBytes = 4096;
inline constexpr std::size_t kMaxTitleBytes = 1024;
inline constexpr std::size_t kMaxLinkBytes = 8192;
inline constexpr std::size_t kMaxPublishedAtBytes = 128;
inline constexpr std::size_t kMaxAuthorBytes = 512;
inline constexpr std::size_t kMaxStoredContentBytes = 1024;
inline constexpr std::string_view kContentCappedTag = "\n\n[content capped]";

inline bool hasSuspiciousControlBytes(const std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](const char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte == '\n' || byte == '\r' || byte == '\t') {
      return false;
    }
    return byte < 0x20 || byte == 0x7f;
  });
}

inline bool validateField(const std::string_view value,
                          const std::size_t max_bytes,
                          const char *const field_name,
                          std::string &rejection_reason) {
  if (value.size() > max_bytes) {
    rejection_reason = std::string{field_name} + " exceeds maximum allowed size";
    return false;
  }
  if (hasSuspiciousControlBytes(value)) {
    rejection_reason = std::string{field_name} + " contains suspicious control characters";
    return false;
  }
  return true;
}

inline std::size_t trimToUtf8Boundary(const std::string_view value, const std::size_t max_bytes) {
  if (value.size() <= max_bytes) {
    return value.size();
  }

  std::size_t result = max_bytes;
  while (result > 0 && (static_cast<unsigned char>(value[result]) & 0xc0U) == 0x80U) {
    --result;
  }
  if (result == 0) {
    return max_bytes;
  }
  return result;
}

inline std::string capContent(const std::string_view value, bool &content_capped) {
  if (value.size() <= kMaxStoredContentBytes) {
    return std::string{value};
  }

  content_capped = true;
  const auto suffix_bytes = kContentCappedTag.size();
  if (suffix_bytes >= kMaxStoredContentBytes) {
    return std::string{kContentCappedTag.substr(0, kMaxStoredContentBytes)};
  }

  const auto prefix_limit = kMaxStoredContentBytes - suffix_bytes;
  const auto prefix_bytes = trimToUtf8Boundary(value, prefix_limit);
  std::string capped;
  capped.reserve(prefix_bytes + suffix_bytes);
  capped.append(value.substr(0, prefix_bytes));
  capped.append(kContentCappedTag);
  return capped;
}

inline std::string normalizeContentControlBytes(const std::string_view value, bool &modified) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if ((byte < 0x20 || byte == 0x7f) && byte != '\n' && byte != '\r' && byte != '\t') {
      normalized.push_back(' ');
      modified = true;
    } else {
      normalized.push_back(ch);
    }
  }
  return normalized;
}

}  // namespace detail

inline ArticleSanitizationResult sanitizeArticleForStorage(ArticleRecord article) {
  ArticleSanitizationResult result;
  const auto original_content_size = article.content.size();
  result.article = std::move(article);

  if (!detail::validateField(result.article.guid, detail::kMaxGuidBytes, "guid", result.rejection_reason)
      || !detail::validateField(result.article.title, detail::kMaxTitleBytes, "title", result.rejection_reason)
      || !detail::validateField(result.article.link_url, detail::kMaxLinkBytes, "link_url", result.rejection_reason)
      || !detail::validateField(result.article.published_at,
                                detail::kMaxPublishedAtBytes,
                                "published_at",
                                result.rejection_reason)
      || !detail::validateField(result.article.author, detail::kMaxAuthorBytes, "author", result.rejection_reason)) {
    return result;
  }

  result.article.content
    = detail::normalizeContentControlBytes(result.article.content, result.content_control_bytes_normalized);

  std::array<std::pair<std::string *, const char *>, 3> internal_fields{{
    {&result.article.article_id, "article_id"},
    {&result.article.user_id, "user_id"},
    {&result.article.node_id, "node_id"},
  }};
  for (auto &[field, field_name] : internal_fields) {
    if (detail::hasSuspiciousControlBytes(*field)) {
      result.rejection_reason = std::string{field_name} + " contains suspicious control characters";
      return result;
    }
  }

  result.article.content = detail::capContent(result.article.content, result.content_capped);
  result.modified = result.content_control_bytes_normalized
                    || result.content_capped
                    || result.article.content.size() != original_content_size;
  result.accepted = true;
  return result;
}

inline std::size_t maxStoredArticleContentBytes() noexcept {
  return detail::kMaxStoredContentBytes;
}

}  // namespace onerss::backend
