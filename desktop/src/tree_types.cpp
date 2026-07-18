#include "tree_types.h"

#include <algorithm>

namespace onerss::desktop {

TreeNodeData fromProto(const onerss::pb::TreeNode &node) {
  return TreeNodeData{
    .node_id = node.nodeId(),
    .parent_id = node.parentId(),
    .type = node.type(),
    .title = node.title(),
    .feed_url = node.feedUrl(),
    .comment = node.comment(),
    .use_default_refresh_interval = node.useDefaultRefreshInterval(),
    .refresh_interval_hours = static_cast<int>(node.refreshIntervalHours() == 0 ? 12 : node.refreshIntervalHours()),
    .archive_mode = static_cast<int>(node.archiveMode()),
    .archive_limit = static_cast<int>(node.archiveLimit()),
    .synthetic = false,
  };
}

onerss::pb::TreeNode toProto(const TreeNodeData &node) {
  onerss::pb::TreeNode proto;
  proto.setNodeId(node.node_id);
  proto.setParentId(node.parent_id);
  proto.setType(node.type);
  proto.setTitle(node.title);
  proto.setFeedUrl(node.feed_url);
  proto.setComment(node.comment);
  proto.setUseDefaultRefreshInterval(node.use_default_refresh_interval);
  proto.setRefreshIntervalHours(static_cast<QtProtobuf::uint32>(node.refresh_interval_hours));
  proto.setArchiveMode(static_cast<onerss::pb::ArchiveModeGadget::ArchiveMode>(node.archive_mode));
  proto.setArchiveLimit(static_cast<QtProtobuf::uint32>(std::max(0, node.archive_limit)));
  return proto;
}

}  // namespace onerss::desktop
