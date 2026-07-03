#include "tree_types.h"

namespace onerss::desktop {

TreeNodeData fromProto(const onerss::pb::TreeNode &node) {
  return TreeNodeData{
    .node_id = QString::fromStdString(node.node_id()),
    .parent_id = QString::fromStdString(node.parent_id()),
    .type = node.type(),
    .title = QString::fromStdString(node.title()),
    .feed_url = QString::fromStdString(node.feed_url()),
    .comment = QString::fromStdString(node.comment()),
    .use_default_refresh_interval = node.use_default_refresh_interval(),
    .refresh_interval_hours = static_cast<int>(node.refresh_interval_hours() == 0 ? 12 : node.refresh_interval_hours()),
    .synthetic = false,
  };
}

onerss::pb::TreeNode toProto(const TreeNodeData &node) {
  onerss::pb::TreeNode proto;
  proto.set_node_id(node.node_id.toStdString());
  proto.set_parent_id(node.parent_id.toStdString());
  proto.set_type(node.type);
  proto.set_title(node.title.toStdString());
  proto.set_feed_url(node.feed_url.toStdString());
  proto.set_comment(node.comment.toStdString());
  proto.set_use_default_refresh_interval(node.use_default_refresh_interval);
  proto.set_refresh_interval_hours(static_cast<std::uint32_t>(node.refresh_interval_hours));
  return proto;
}

}  // namespace onerss::desktop
