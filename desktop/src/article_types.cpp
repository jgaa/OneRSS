#include "article_types.h"

namespace onerss::desktop {

ArticleData fromProto(const onerss::pb::Article &article) {
  return ArticleData{
    .article_id = article.articleId(),
    .node_id = article.nodeId(),
    .feed_title = {},
    .title = article.title(),
    .link_url = article.linkUrl(),
    .published_at = article.publishedAt(),
    .author = article.author(),
    .content = article.content(),
    .is_read = article.isRead(),
    .is_queued = article.isQueued(),
  };
}

}  // namespace onerss::desktop
