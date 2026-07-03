#pragma once

#include "onerss.pb.h"

#include <QString>
#include <QVariantMap>
#include <QVector>

namespace onerss::desktop {

struct TreeNodeData {
  QString node_id;
  QString parent_id;
  onerss::pb::TreeNode::Type type = onerss::pb::TreeNode::TYPE_FOLDER;
  QString title;
  QString feed_url;
  QString comment;
  bool use_default_refresh_interval = true;
  int refresh_interval_hours = 12;
  bool synthetic = false;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap map;
    map.insert(QStringLiteral("nodeId"), node_id);
    map.insert(QStringLiteral("parentId"), parent_id);
    map.insert(QStringLiteral("type"), static_cast<int>(type));
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("feedUrl"), feed_url);
    map.insert(QStringLiteral("comment"), comment);
    map.insert(QStringLiteral("useDefaultRefreshInterval"), use_default_refresh_interval);
    map.insert(QStringLiteral("refreshIntervalHours"), refresh_interval_hours);
    map.insert(QStringLiteral("synthetic"), synthetic);
    return map;
  }
};

TreeNodeData fromProto(const onerss::pb::TreeNode &node);
onerss::pb::TreeNode toProto(const TreeNodeData &node);

}  // namespace onerss::desktop
