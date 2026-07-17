#include "article_list_model.h"

#include <QDateTime>

#include <algorithm>

namespace onerss::desktop {
namespace {

QDateTime parsePublishedAt(const QString &value) {
  if (value.isEmpty()) {
    return {};
  }

  auto parsed = QDateTime::fromString(value, Qt::ISODate);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::RFC2822Date);
  }
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::TextDate);
  }
  return parsed;
}

void sortNewestFirst(QVector<ArticleData> &articles) {
  std::stable_sort(articles.begin(), articles.end(), [](const auto &lhs, const auto &rhs) {
    const auto lhs_time = parsePublishedAt(lhs.published_at);
    const auto rhs_time = parsePublishedAt(rhs.published_at);
    if (lhs_time.isValid() && rhs_time.isValid() && lhs_time != rhs_time) {
      return lhs_time > rhs_time;
    }
    if (lhs_time.isValid() != rhs_time.isValid()) {
      return lhs_time.isValid();
    }
    return lhs.article_id < rhs.article_id;
  });
}

}  // namespace

ArticleListModel::ArticleListModel(QObject *parent) : QAbstractListModel(parent) {}

int ArticleListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : articles_.size();
}

QVariant ArticleListModel::data(const QModelIndex &index, const int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= articles_.size()) {
    return {};
  }

  const auto &article = articles_.at(index.row());
  switch (role) {
    case ArticleIdRole:
      return article.article_id;
    case NodeIdRole:
      return article.node_id;
    case FeedTitleRole:
      return article.feed_title;
    case TitleRole:
      return article.title;
    case LinkUrlRole:
      return article.link_url;
    case PublishedAtRole:
      return article.published_at;
    case AuthorRole:
      return article.author;
    case ContentRole:
      return article.content;
    case IsReadRole:
      return article.is_read;
    case IsQueuedRole:
      return article.is_queued;
    case SelectedRole:
      return index.row() == selected_row_;
    default:
      return {};
  }
}

QHash<int, QByteArray> ArticleListModel::roleNames() const {
  return {
    {ArticleIdRole, "articleId"},
    {NodeIdRole, "nodeId"},
    {FeedTitleRole, "feedTitle"},
    {TitleRole, "title"},
    {LinkUrlRole, "linkUrl"},
    {PublishedAtRole, "publishedAt"},
    {AuthorRole, "author"},
    {ContentRole, "content"},
    {IsReadRole, "isRead"},
    {IsQueuedRole, "isQueued"},
    {SelectedRole, "selected"},
  };
}

int ArticleListModel::selectedRow() const {
  return selected_row_;
}

void ArticleListModel::setArticles(const QVector<ArticleData> &articles) {
  beginResetModel();
  articles_ = articles;
  sortNewestFirst(articles_);
  selected_row_ = articles_.isEmpty() ? -1 : 0;
  endResetModel();
  emit selectedRowChanged();
}

void ArticleListModel::appendArticles(const QVector<ArticleData> &articles) {
  if (articles.isEmpty()) {
    return;
  }

  const auto start = articles_.size();
  const auto end = start + articles.size() - 1;
  beginInsertRows(QModelIndex{}, start, end);
  for (const auto &article : articles) {
    articles_.push_back(article);
  }
  endInsertRows();
}

QVariantMap ArticleListModel::articleAt(const int row) const {
  if (row < 0 || row >= articles_.size()) {
    return {};
  }
  return articles_.at(row).toVariantMap();
}

bool ArticleListModel::isReadAt(const int row) const {
  if (row < 0 || row >= articles_.size()) {
    return false;
  }
  return articles_.at(row).is_read;
}

bool ArticleListModel::isQueuedAt(const int row) const {
  if (row < 0 || row >= articles_.size()) {
    return false;
  }
  return articles_.at(row).is_queued;
}

bool ArticleListModel::markReadByRow(const int row) {
  if (row < 0 || row >= articles_.size() || articles_[row].is_read) {
    return false;
  }
  articles_[row].is_read = true;
  emit dataChanged(index(row, 0), index(row, 0), {IsReadRole});
  return true;
}

bool ArticleListModel::markUnreadByRow(const int row) {
  if (row < 0 || row >= articles_.size() || !articles_[row].is_read) {
    return false;
  }
  articles_[row].is_read = false;
  emit dataChanged(index(row, 0), index(row, 0), {IsReadRole});
  return true;
}

bool ArticleListModel::markQueuedByRow(const int row) {
  if (row < 0 || row >= articles_.size() || articles_[row].is_queued) {
    return false;
  }
  articles_[row].is_queued = true;
  emit dataChanged(index(row, 0), index(row, 0), {IsQueuedRole});
  return true;
}

bool ArticleListModel::markUnqueuedByRow(const int row) {
  if (row < 0 || row >= articles_.size() || !articles_[row].is_queued) {
    return false;
  }
  articles_[row].is_queued = false;
  emit dataChanged(index(row, 0), index(row, 0), {IsQueuedRole});
  return true;
}

int ArticleListModel::unreadCount() const {
  int unread = 0;
  for (const auto &article : articles_) {
    if (!article.is_read) {
      ++unread;
    }
  }
  return unread;
}

void ArticleListModel::markAllRead() {
  int first = -1;
  int last = -1;
  for (int i = 0; i < articles_.size(); ++i) {
    if (articles_[i].is_read) {
      continue;
    }
    articles_[i].is_read = true;
    if (first < 0) {
      first = i;
    }
    last = i;
  }
  if (first >= 0) {
    emit dataChanged(index(first, 0), index(last, 0), {IsReadRole});
  }
}

void ArticleListModel::selectRow(const int row) {
  const auto normalized_row = row >= 0 && row < articles_.size() ? row : -1;
  if (selected_row_ == normalized_row) {
    return;
  }

  const auto previous_row = selected_row_;
  selected_row_ = normalized_row;
  emitRowChanged(previous_row);
  emitRowChanged(selected_row_);
  emit selectedRowChanged();
}

void ArticleListModel::emitRowChanged(const int row) {
  if (row < 0 || row >= articles_.size()) {
    return;
  }

  const auto model_index = index(row, 0);
  emit dataChanged(model_index, model_index, {SelectedRole});
}

}  // namespace onerss::desktop
