#pragma once

#include "tree_types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>

namespace onerss::desktop {

class FeedTreeModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Roles {
    NodeIdRole = Qt::UserRole + 1,
    ParentIdRole,
    TitleRole,
    NodeTypeRole,
    FeedUrlRole,
    CommentRole,
    DepthRole,
    ExpandedRole,
    HasChildrenRole,
    SyntheticRole
  };

  explicit FeedTreeModel(QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void loadNodes(const QVector<TreeNodeData> &nodes);
  void upsertNode(const TreeNodeData &node);
  void removeNode(const QString &node_id);

  Q_INVOKABLE void toggleExpanded(const QString &node_id);
  Q_INVOKABLE void expandNode(const QString &node_id);
  Q_INVOKABLE QVariantMap nodeData(const QString &node_id) const;
  Q_INVOKABLE bool canReparent(const QString &node_id, const QString &parent_id) const;
  [[nodiscard]] QString titleForNode(const QString &node_id) const;

 private:
  struct VisibleNode {
    TreeNodeData node;
    int depth = 0;
    bool expanded = false;
    bool has_children = false;
  };

  void rebuildVisible();
  void appendVisible(const QString &parent_id, int depth);
  [[nodiscard]] QVector<QString> childrenOf(const QString &parent_id) const;
  [[nodiscard]] bool isDescendantOf(const QString &node_id, const QString &ancestor_id) const;
  void removeDescendants(const QString &node_id);

  TreeNodeData root_node_;
  QHash<QString, TreeNodeData> nodes_;
  QVector<VisibleNode> visible_nodes_;
  QSet<QString> expanded_nodes_;
};

}  // namespace onerss::desktop
