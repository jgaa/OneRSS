#pragma once

#include "onerss.qpb.h"

#include <QString>
#include <QVariantMap>

namespace onerss::desktop {

struct ArticleData {
  QString article_id;
  QString node_id;
  QString title;
  QString link_url;
  QString published_at;
  QString author;
  QString content;
  bool is_read = false;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap map;
    map.insert(QStringLiteral("articleId"), article_id);
    map.insert(QStringLiteral("nodeId"), node_id);
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("linkUrl"), link_url);
    map.insert(QStringLiteral("publishedAt"), published_at);
    map.insert(QStringLiteral("author"), author);
    map.insert(QStringLiteral("content"), content);
    map.insert(QStringLiteral("isRead"), is_read);
    return map;
  }
};

ArticleData fromProto(const onerss::pb::Article &article);

}  // namespace onerss::desktop
