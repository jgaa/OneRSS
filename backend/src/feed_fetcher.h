#pragma once

#include "article_types.h"
#include "tree_repository.h"

#include <string>
#include <vector>

namespace onerss::backend {

class FeedFetcher final {
 public:
  [[nodiscard]] std::vector<ArticleRecord> fetchArticles(const TreeNodeRecord &feed_node) const;
};

}  // namespace onerss::backend
