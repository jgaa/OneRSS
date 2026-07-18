#pragma once

#include "article_types.h"

#include <QAbstractListModel>
#include <QVector>

namespace onerss::desktop {

class ArticleListModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedRowChanged)

 public:
  enum Roles {
    ArticleIdRole = Qt::UserRole + 1,
    NodeIdRole,
    FeedTitleRole,
    TitleRole,
    LinkUrlRole,
    PublishedAtRole,
    AuthorRole,
    ContentRole,
    IsReadRole,
    IsQueuedRole,
    SelectedRole
  };

  explicit ArticleListModel(QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] int selectedRow() const;

  void setArticles(const QVector<ArticleData> &articles);
  void appendArticles(const QVector<ArticleData> &articles);
  [[nodiscard]] QVariantMap articleAt(int row) const;
  [[nodiscard]] bool isReadAt(int row) const;
  [[nodiscard]] bool isQueuedAt(int row) const;
  [[nodiscard]] bool markReadByRow(int row);
  [[nodiscard]] bool markUnreadByRow(int row);
  [[nodiscard]] bool markQueuedByRow(int row);
  [[nodiscard]] bool markUnqueuedByRow(int row);
  [[nodiscard]] int unreadCount() const;
  void markAllRead();
  Q_INVOKABLE void selectRow(int row);

 signals:
  void selectedRowChanged();

 private:
  void mergeArticles(const QVector<ArticleData> &articles, bool remove_missing);
  void emitRowChanged(int row);

  QVector<ArticleData> articles_;
  int selected_row_ = -1;
};

}  // namespace onerss::desktop
