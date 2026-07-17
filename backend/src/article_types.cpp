#include "article_types.h"

namespace onerss::backend {

onerss::pb::Article toProto(const ArticleRecord &record) {
  onerss::pb::Article article;
  article.set_article_id(record.article_id);
  article.set_node_id(record.node_id);
  article.set_title(record.title);
  article.set_link_url(record.link_url);
  article.set_published_at(record.published_at);
  article.set_author(record.author);
  article.set_content(record.content);
  article.set_is_read(record.is_read);
  article.set_is_queued(record.is_queued);
  return article;
}

}  // namespace onerss::backend
