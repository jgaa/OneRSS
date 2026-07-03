#include "article_types.h"

namespace onerss::desktop {

ArticleData fromProto(const onerss::pb::Article &article) {
  return ArticleData{
    .article_id = QString::fromStdString(article.article_id()),
    .node_id = QString::fromStdString(article.node_id()),
    .title = QString::fromStdString(article.title()),
    .link_url = QString::fromStdString(article.link_url()),
    .published_at = QString::fromStdString(article.published_at()),
    .author = QString::fromStdString(article.author()),
    .content = QString::fromStdString(article.content()),
    .is_read = article.is_read(),
  };
}

}  // namespace onerss::desktop
