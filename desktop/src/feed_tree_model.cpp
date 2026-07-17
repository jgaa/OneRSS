#include "feed_tree_model.h"

#include <algorithm>

namespace onerss::desktop {

FeedTreeModel::FeedTreeModel(QObject *parent) : QAbstractListModel(parent) {
  root_node_ = TreeNodeData{
    .node_id = QStringLiteral("__root__"),
    .parent_id = {},
    .type = onerss::pb::TreeNode::Type::TYPE_FOLDER,
    .title = tr("All Feeds"),
    .feed_url = {},
    .comment = {},
    .synthetic = true,
  };
  queue_node_ = TreeNodeData{
    .node_id = QStringLiteral("__queue__"),
    .parent_id = {},
    .type = onerss::pb::TreeNode::Type::TYPE_FOLDER,
    .title = tr("Queue"),
    .feed_url = {},
    .comment = {},
    .synthetic = true,
  };
  expanded_nodes_.insert(root_node_.node_id);
  rebuildVisible();
}

QString FeedTreeModel::filterText() const {
  return filter_text_;
}

void FeedTreeModel::setFilterText(const QString &value) {
  const auto normalized = value.trimmed();
  if (filter_text_ == normalized) {
    return;
  }
  filter_text_ = normalized;
  rebuildVisible();
  emit filterTextChanged();
}

int FeedTreeModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : visible_nodes_.size();
}

QVariant FeedTreeModel::data(const QModelIndex &index, const int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= visible_nodes_.size()) {
    return {};
  }

  const auto &item = visible_nodes_.at(index.row());
  switch (role) {
    case NodeIdRole:
      return item.node.node_id;
    case ParentIdRole:
      return item.node.parent_id;
    case TitleRole:
      return item.node.title;
    case NodeTypeRole:
      return static_cast<int>(item.node.type);
    case FeedUrlRole:
      return item.node.feed_url;
    case CommentRole:
      return item.node.comment;
    case DepthRole:
      return item.depth;
    case ExpandedRole:
      return item.expanded;
    case HasChildrenRole:
      return item.has_children;
    case SyntheticRole:
      return item.node.synthetic;
    default:
      return {};
  }
}

QHash<int, QByteArray> FeedTreeModel::roleNames() const {
  return {
    {NodeIdRole, "nodeId"},
    {ParentIdRole, "parentId"},
    {TitleRole, "title"},
    {NodeTypeRole, "nodeType"},
    {FeedUrlRole, "feedUrl"},
    {CommentRole, "comment"},
    {DepthRole, "depth"},
    {ExpandedRole, "expanded"},
    {HasChildrenRole, "hasChildren"},
    {SyntheticRole, "synthetic"},
  };
}

void FeedTreeModel::loadNodes(const QVector<TreeNodeData> &nodes) {
  nodes_.clear();
  for (const auto &node : nodes) {
    nodes_.insert(node.node_id, node);
  }
  rebuildVisible();
}

void FeedTreeModel::upsertNode(const TreeNodeData &node) {
  nodes_.insert(node.node_id, node);
  rebuildVisible();
}

void FeedTreeModel::removeNode(const QString &node_id) {
  removeDescendants(node_id);
  nodes_.remove(node_id);
  expanded_nodes_.remove(node_id);
  rebuildVisible();
}

void FeedTreeModel::toggleExpanded(const QString &node_id) {
  if (node_id.isEmpty()) {
    return;
  }
  if (expanded_nodes_.contains(node_id)) {
    expanded_nodes_.remove(node_id);
  } else {
    expanded_nodes_.insert(node_id);
  }
  rebuildVisible();
}

void FeedTreeModel::expandNode(const QString &node_id) {
  if (node_id.isEmpty() || expanded_nodes_.contains(node_id)) {
    return;
  }
  expanded_nodes_.insert(node_id);
  rebuildVisible();
}

QVariantMap FeedTreeModel::nodeData(const QString &node_id) const {
  if (node_id == root_node_.node_id) {
    return root_node_.toVariantMap();
  }
  if (node_id == queue_node_.node_id) {
    return queue_node_.toVariantMap();
  }
  const auto it = nodes_.find(node_id);
  return it != nodes_.end() ? it->toVariantMap() : QVariantMap{};
}

bool FeedTreeModel::canReparent(const QString &node_id, const QString &parent_id) const {
  if (node_id == queue_node_.node_id || parent_id == queue_node_.node_id) {
    return false;
  }
  const auto node_it = nodes_.find(node_id);
  if (node_it == nodes_.end()) {
    return false;
  }
  if (node_id == parent_id || node_it->parent_id == parent_id) {
    return false;
  }
  if (!parent_id.isEmpty()) {
    if (parent_id == root_node_.node_id) {
      return false;
    }
    const auto parent_it = nodes_.find(parent_id);
    if (parent_it == nodes_.end()) {
      return false;
    }
    if (parent_it->type != onerss::pb::TreeNode::Type::TYPE_FOLDER) {
      return false;
    }
    if (isDescendantOf(parent_id, node_id)) {
      return false;
    }
  }
  return true;
}

QString FeedTreeModel::titleForNode(const QString &node_id) const {
  if (node_id == root_node_.node_id) {
    return root_node_.title;
  }
  if (node_id == queue_node_.node_id) {
    return queue_node_.title;
  }
  const auto it = nodes_.find(node_id);
  return it != nodes_.end() ? it->title : QString{};
}

void FeedTreeModel::rebuildVisible() {
  beginResetModel();
  visible_nodes_.clear();
  visible_nodes_.push_back(VisibleNode{
    .node = root_node_,
    .depth = 0,
    .expanded = true,
    .has_children = !childrenOf(QString{}).isEmpty(),
  });
  if (filter_text_.isEmpty()) {
    appendVisible(QString{}, 1);
  } else {
    appendFiltered(QString{}, 1);
  }
  visible_nodes_.push_back(VisibleNode{
    .node = queue_node_,
    .depth = 0,
    .expanded = false,
    .has_children = false,
  });
  endResetModel();
}

void FeedTreeModel::appendVisible(const QString &parent_id, const int depth) {
  const auto children = childrenOf(parent_id);
  for (const auto &child_id : children) {
    const auto node = nodes_.value(child_id);
    const bool has_children = !childrenOf(child_id).isEmpty();
    const bool expanded = expanded_nodes_.contains(child_id);
    visible_nodes_.push_back(VisibleNode{
      .node = node,
      .depth = depth,
      .expanded = expanded,
      .has_children = has_children,
    });
    if (has_children && expanded) {
      appendVisible(child_id, depth + 1);
    }
  }
}

void FeedTreeModel::appendFiltered(const QString &parent_id, const int depth) {
  const auto children = childrenOf(parent_id);
  for (const auto &child_id : children) {
    const auto node = nodes_.value(child_id);
    const bool has_children = !childrenOf(child_id).isEmpty();
    const bool subtree_matches = subtreeMatchesFilter(child_id);
    if (!subtree_matches) {
      continue;
    }

    visible_nodes_.push_back(VisibleNode{
      .node = node,
      .depth = depth,
      .expanded = has_children,
      .has_children = has_children,
    });

    if (has_children) {
      appendFiltered(child_id, depth + 1);
    }
  }
}

QVector<QString> FeedTreeModel::childrenOf(const QString &parent_id) const {
  QVector<QString> children;
  for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it) {
    if (it->parent_id == parent_id) {
      children.push_back(it.key());
    }
  }

  std::sort(children.begin(), children.end(), [this](const auto &lhs, const auto &rhs) {
    const auto &a = nodes_[lhs];
    const auto &b = nodes_[rhs];
    if (a.type != b.type) {
      return a.type < b.type;
    }
    return a.title.localeAwareCompare(b.title) < 0;
  });
  return children;
}

bool FeedTreeModel::isDescendantOf(const QString &node_id, const QString &ancestor_id) const {
  auto current_id = node_id;
  while (!current_id.isEmpty()) {
    if (current_id == ancestor_id) {
      return true;
    }
    const auto current_it = nodes_.find(current_id);
    if (current_it == nodes_.end()) {
      return false;
    }
    current_id = current_it->parent_id;
  }
  return false;
}

bool FeedTreeModel::subtreeMatchesFilter(const QString &node_id) const {
  const auto node_it = nodes_.find(node_id);
  if (node_it == nodes_.end()) {
    return false;
  }
  if (nodeMatchesFilter(*node_it)) {
    return true;
  }
  const auto children = childrenOf(node_id);
  return std::any_of(children.cbegin(), children.cend(), [this](const auto &child_id) {
    return subtreeMatchesFilter(child_id);
  });
}

bool FeedTreeModel::nodeMatchesFilter(const TreeNodeData &node) const {
  return node.title.contains(filter_text_, Qt::CaseInsensitive);
}

void FeedTreeModel::removeDescendants(const QString &node_id) {
  const auto children = childrenOf(node_id);
  for (const auto &child_id : children) {
    removeDescendants(child_id);
    nodes_.remove(child_id);
    expanded_nodes_.remove(child_id);
  }
}

}  // namespace onerss::desktop
