#pragma once

#include "onerss.qpb.h"

#include <QString>
#include <QVariantMap>

namespace onerss::desktop {

struct ArticleData {
  QString article_id;
  QString node_id;
  QString feed_title;
  QString title;
  QString link_url;
  QString published_at;
  QString author;
  QString content;
  bool is_read = false;
  bool is_queued = false;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap map;
    map.insert(QStringLiteral("articleId"), article_id);
    map.insert(QStringLiteral("nodeId"), node_id);
    map.insert(QStringLiteral("feedTitle"), feed_title);
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("linkUrl"), link_url);
    map.insert(QStringLiteral("publishedAt"), published_at);
    map.insert(QStringLiteral("author"), author);
    map.insert(QStringLiteral("content"), content);
    map.insert(QStringLiteral("isRead"), is_read);
    map.insert(QStringLiteral("isQueued"), is_queued);
    return map;
  }
};

ArticleData fromProto(const onerss::pb::Article &article);

}  // namespace onerss::desktop
