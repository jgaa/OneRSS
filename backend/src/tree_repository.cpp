#include "tree_repository.h"

#include <algorithm>
#include <stdexcept>

namespace onerss::backend {

onerss::pb::TreeNode toProto(const TreeNodeRecord &record) {
  onerss::pb::TreeNode node;
  node.set_node_id(record.node_id);
  node.set_parent_id(record.parent_id);
  node.set_type(record.type);
  node.set_title(record.title);
  node.set_feed_url(record.feed_url);
  node.set_comment(record.comment);
  node.set_use_default_refresh_interval(record.use_default_refresh_interval);
  node.set_refresh_interval_hours(record.refresh_interval_hours);
  node.set_archive_mode(record.archive_mode);
  node.set_archive_limit(record.archive_limit);
  return node;
}

TreeNodeRecord fromProto(const std::string &user_id, const onerss::pb::TreeNode &node) {
  if (node.node_id().empty()) {
    throw std::runtime_error{"node_id is required"};
  }
  if (node.title().empty()) {
    throw std::runtime_error{"title is required"};
  }
  if (node.type() == onerss::pb::TreeNode::TYPE_FEED && node.feed_url().empty()) {
    throw std::runtime_error{"feed_url is required for feed nodes"};
  }

  return TreeNodeRecord{
    .node_id = node.node_id(),
    .user_id = user_id,
    .parent_id = node.parent_id(),
    .type = node.type(),
    .title = node.title(),
    .feed_url = node.feed_url(),
    .comment = node.comment(),
    .use_default_refresh_interval = node.use_default_refresh_interval(),
    .refresh_interval_hours = node.refresh_interval_hours() == 0 ? 12 : node.refresh_interval_hours(),
    .archive_mode = static_cast<onerss::pb::ArchiveMode>(std::clamp(static_cast<int>(node.archive_mode()),
                                                                     static_cast<int>(onerss::pb::ARCHIVE_MODE_USE_DEFAULT),
                                                                     static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED))),
    .archive_limit = node.archive_limit(),
  };
}

}  // namespace onerss::backend
