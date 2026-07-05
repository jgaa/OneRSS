#include "feed_title_resolver.h"
#include "main_view_model.h"

#include "logging.h"

#include <QNetworkInformation>
#include <QMetaObject>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>
#include <typeinfo>

#include <thread>

namespace onerss::desktop {

MainViewModel::MainViewModel(QObject *parent) : QObject(parent) {
  health_check_timer_.setInterval(60 * 1000);
  connect(&health_check_timer_, &QTimer::timeout, this, [this]() { performHealthCheck(); });

  reconnect_timer_.setSingleShot(true);
  connect(&reconnect_timer_, &QTimer::timeout, this, [this]() { reconnect(); });

  static_cast<void>(QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability));
  if (auto *network_information = QNetworkInformation::instance()) {
    connect(network_information,
            &QNetworkInformation::reachabilityChanged,
            this,
            [this](QNetworkInformation::Reachability) { handleNetworkReachabilityChanged(); });
  }

  connect(&article_list_model_,
          &ArticleListModel::selectedRowChanged,
          this,
          [this]() { setPreviewFromArticle(article_list_model_.articleAt(article_list_model_.selectedRow())); });
  app_client_.onUserSettingsUpdated = [this](const UserSettingsData &settings, const QString &) {
    QMetaObject::invokeMethod(this,
                              [this, settings]() {
                                if (default_refresh_interval_hours_ != settings.default_refresh_interval_hours) {
                                  default_refresh_interval_hours_ = settings.default_refresh_interval_hours;
                                  emit userSettingsChanged();
                                }
                              },
                              Qt::QueuedConnection);
  };
  app_client_.onNodeUpserted = [this](const TreeNodeData &node, const QString &origin_device_id) {
    if (origin_device_id == app_client_.deviceId()) {
      return;
    }
    QMetaObject::invokeMethod(this, [this, node]() { feed_tree_model_.upsertNode(node); }, Qt::QueuedConnection);
  };
  app_client_.onNodeDeleted = [this](const QString &node_id, const QString &origin_device_id) {
    if (origin_device_id == app_client_.deviceId()) {
      return;
    }
    QMetaObject::invokeMethod(this, [this, node_id]() { feed_tree_model_.removeNode(node_id); }, Qt::QueuedConnection);
  };
  app_client_.onArticlesUpdated = [this](const QString &node_id, const QString &origin_device_id) {
    QMetaObject::invokeMethod(this,
                              [this, node_id, origin_device_id]() {
                                LOG_TRACE << "Handling articles updated notification selected_node_id="
                                          << selected_node_id_.toStdString()
                                          << " node_id=" << node_id.toStdString()
                                          << " origin_device_id=" << origin_device_id.toStdString()
                                          << " local_device_id=" << app_client_.deviceId().toStdString();
                                refreshUnreadCount();
                                if (origin_device_id == app_client_.deviceId()) {
                                  LOG_TRACE << "Ignoring article reload for self-originated notification node_id="
                                            << node_id.toStdString();
                                  return;
                                }
                                if (selected_node_id_ == QStringLiteral("__root__")
                                    || selected_node_id_ == node_id
                                    || origin_device_id.isEmpty()) {
                                  LOG_TRACE << "Reloading articles after notification for node_id="
                                            << node_id.toStdString();
                                  reloadArticlesForCurrentNode();
                                } else {
                                  LOG_TRACE << "Ignoring article reload for notification node_id="
                                            << node_id.toStdString()
                                            << " because the current node is different";
                                }
                              },
                              Qt::QueuedConnection);
  };
  app_client_.onUserNotification = [this](const UiStatusMessage &message) {
    QMetaObject::invokeMethod(this, [this, message]() { setStatusBarMessage(message); }, Qt::QueuedConnection);
  };
  app_client_.onConnectionLost = [this](const QString &reason) {
    QMetaObject::invokeMethod(this,
                              [this, reason]() {
                                setConnectionStatus(tr("Disconnected"));
                                setStatusBarMessage(UiStatusMessage{
                                  .severity = UiStatusMessage::Severity::Warning,
                                  .text = tr("Connection to the server was lost."),
                                  .detail = reason
                                });
                                if (isNetworkReachable()) {
                                  scheduleReconnect(1000);
                                }
                              },
                              Qt::QueuedConnection);
  };
  reconnect();
}

MainViewModel::~MainViewModel() {
  app_client_.stop();
}

FeedTreeModel *MainViewModel::feedTreeModel() {
  return &feed_tree_model_;
}

ArticleListModel *MainViewModel::articleListModel() {
  return &article_list_model_;
}

int MainViewModel::connectionState() const {
  if (connection_status_ == tr("Connected")) {
    return 2;
  }
  if (connection_status_ == tr("Connecting to server...")) {
    return 1;
  }
  return 0;
}

QString MainViewModel::connectionStatus() const {
  return connection_status_;
}

QString MainViewModel::statusBarText() const {
  return status_bar_message_.text;
}

QString MainViewModel::statusBarDetail() const {
  return status_bar_message_.detail;
}

int MainViewModel::statusBarSeverity() const {
  return static_cast<int>(status_bar_message_.severity);
}

bool MainViewModel::hasStatusBarDetail() const {
  return !status_bar_message_.detail.isEmpty();
}

QString MainViewModel::selectedNodeId() const {
  return selected_node_id_;
}

int MainViewModel::defaultRefreshIntervalHours() const {
  return default_refresh_interval_hours_;
}

int MainViewModel::unreadCount() const {
  return unread_count_;
}

QString MainViewModel::previewTitle() const {
  return preview_title_;
}

QString MainViewModel::previewMeta() const {
  return preview_meta_;
}

QString MainViewModel::previewContent() const {
  return preview_content_;
}

bool MainViewModel::hasPreview() const {
  return !preview_title_.isEmpty() || !preview_content_.isEmpty();
}

bool MainViewModel::selectedArticleIsRead() const {
  return article_list_model_.isReadAt(article_list_model_.selectedRow());
}

bool MainViewModel::canLoadMoreArticles() const {
  return can_load_more_articles_;
}

bool MainViewModel::loadingMoreArticles() const {
  return loading_more_articles_;
}

void MainViewModel::reconnect() {
  if (reconnect_in_progress_) {
    return;
  }
  reconnect_timer_.stop();
  reconnect_in_progress_ = true;
  runAsync([this]() {
    try {
      const auto peer = secret_store_.loadPeer();
      if (!peer.isValid()) {
        QMetaObject::invokeMethod(this,
                                  [this]() {
                                    reconnect_in_progress_ = false;
                                    health_check_timer_.stop();
                                    setConnectionStatus(tr("No paired device configured"));
                                    setStatusBarMessage(UiStatusMessage{
                                      .severity = UiStatusMessage::Severity::Warning,
                                      .text = tr("Pair a device before syncing."),
                                      .detail = tr("No stored peer credentials were found.")
                                    });
                                  },
                                  Qt::QueuedConnection);
        return;
      }

      if (!isNetworkReachable()) {
        QMetaObject::invokeMethod(this,
                                  [this]() {
                                    reconnect_in_progress_ = false;
                                    health_check_timer_.stop();
                                    setConnectionStatus(tr("Waiting for network"));
                                    setStatusBarMessage(UiStatusMessage{
                                      .severity = UiStatusMessage::Severity::Warning,
                                      .text = tr("Network unavailable."),
                                      .detail = tr("OneRSS will reconnect when connectivity returns.")
                                    });
                                    scheduleReconnect(60 * 1000);
                                  },
                                  Qt::QueuedConnection);
        return;
      }

      QMetaObject::invokeMethod(this,
                                [this]() { setConnectionStatus(tr("Connecting to server...")); },
                                Qt::QueuedConnection);
      app_client_.stop();
      app_client_.connectAndStart(peer);
      app_client_.ping();
      const auto settings = app_client_.fetchUserSettings();
      const auto unread_count = app_client_.fetchUnreadCount();
      const auto nodes = app_client_.fetchTree();
      QMetaObject::invokeMethod(this,
                                [this, settings, unread_count, nodes]() {
                                  reconnect_in_progress_ = false;
                                  if (default_refresh_interval_hours_ != settings.default_refresh_interval_hours) {
                                    default_refresh_interval_hours_ = settings.default_refresh_interval_hours;
                                    emit userSettingsChanged();
                                  }
                                  if (unread_count_ != unread_count) {
                                    unread_count_ = unread_count;
                                    emit unreadCountChanged();
                                  }
                                  feed_tree_model_.loadNodes(nodes);
                                  setConnectionStatus(tr("Connected"));
                                  health_check_timer_.start();
                                  reloadArticlesForCurrentNode();
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Failed to connect app client: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, message = QString::fromUtf8(error.what())]() {
                                  reconnect_in_progress_ = false;
                                  health_check_timer_.stop();
                                  setConnectionStatus(tr("Connection failed: %1").arg(message));
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Error,
                                    .text = tr("Could not connect to the server."),
                                    .detail = message
                                  });
                                  if (isNetworkReachable()) {
                                    scheduleReconnect(5000);
                                  }
                                },
                                Qt::QueuedConnection);
    }
  });
}

QVariantMap MainViewModel::nodeData(const QString &node_id) const {
  return feed_tree_model_.nodeData(node_id);
}

void MainViewModel::toggleExpanded(const QString &node_id) {
  feed_tree_model_.toggleExpanded(node_id);
}

void MainViewModel::addFolder(const QString &parent_id, const QString &title) {
  runAsync([this, parent_id, title]() {
    try {
      const auto node = app_client_.createFolder(parent_id == QStringLiteral("__root__") ? QString{} : parent_id,
                                                 title);
      QMetaObject::invokeMethod(this, [this, node]() { feed_tree_model_.upsertNode(node); }, Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Add folder failed: " << error.what();
    }
  });
}

void MainViewModel::addFeed(const QString &parent_id,
                            const QString &title,
                            const QString &feed_url,
                            const QString &comment) {
  runAsync([this, parent_id, title, feed_url, comment]() {
    try {
      const auto node = app_client_.createFeed(parent_id == QStringLiteral("__root__") ? QString{} : parent_id,
                                               title,
                                               feed_url,
                                               comment);
      QMetaObject::invokeMethod(this, [this, node]() { feed_tree_model_.upsertNode(node); }, Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Add feed failed: " << error.what();
    }
  });
}

void MainViewModel::renameNode(const QString &node_id, const QString &title) {
  auto node = feed_tree_model_.nodeData(node_id);
  if (node.isEmpty()) {
    return;
  }
  TreeNodeData update;
  update.node_id = node.value(QStringLiteral("nodeId")).toString();
  update.parent_id = node.value(QStringLiteral("parentId")).toString();
  update.type = static_cast<onerss::pb::TreeNode::Type>(node.value(QStringLiteral("type")).toInt());
  update.title = title;
  update.feed_url = node.value(QStringLiteral("feedUrl")).toString();
  update.comment = node.value(QStringLiteral("comment")).toString();
  update.use_default_refresh_interval = node.value(QStringLiteral("useDefaultRefreshInterval")).toBool();
  update.refresh_interval_hours = node.value(QStringLiteral("refreshIntervalHours")).toInt();
  configureFeed(update.node_id,
                update.title,
                update.feed_url,
                update.comment,
                update.use_default_refresh_interval,
                update.refresh_interval_hours);
}

void MainViewModel::configureFeed(const QString &node_id,
                                  const QString &title,
                                  const QString &feed_url,
                                  const QString &comment,
                                  const bool use_default_refresh_interval,
                                  const int refresh_interval_hours) {
  auto node = feed_tree_model_.nodeData(node_id);
  if (node.isEmpty()) {
    return;
  }
  TreeNodeData update;
  update.node_id = node.value(QStringLiteral("nodeId")).toString();
  update.parent_id = node.value(QStringLiteral("parentId")).toString();
  update.type = static_cast<onerss::pb::TreeNode::Type>(node.value(QStringLiteral("type")).toInt());
  update.title = title;
  update.feed_url = feed_url;
  update.comment = comment;
  update.use_default_refresh_interval = use_default_refresh_interval;
  update.refresh_interval_hours = std::max(1, refresh_interval_hours);

  runAsync([this, update]() {
    try {
      const auto node = app_client_.updateNode(update);
      QMetaObject::invokeMethod(this, [this, node]() { feed_tree_model_.upsertNode(node); }, Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Update node failed: " << error.what();
    }
  });
}

void MainViewModel::deleteNode(const QString &node_id) {
  runAsync([this, node_id]() {
    try {
      app_client_.deleteNode(node_id);
      QMetaObject::invokeMethod(this, [this, node_id]() { feed_tree_model_.removeNode(node_id); }, Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Delete node failed: " << error.what();
    }
  });
}

void MainViewModel::refreshFeed(const QString &node_id) {
  runAsync([this, node_id]() {
    try {
      app_client_.refreshFeed(node_id);
      refreshUnreadCount();
      QMetaObject::invokeMethod(this,
                                [this, node_id]() {
                                  if (selected_node_id_ == QStringLiteral("__root__") || selected_node_id_ == node_id) {
                                    reloadArticlesForCurrentNode();
                                  }
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Refresh feed failed: " << error.what();
    }
  });
}

void MainViewModel::moveNode(const QString &node_id, const QString &parent_id) {
  const auto node = feed_tree_model_.nodeData(node_id);
  if (node.isEmpty() || !feed_tree_model_.canReparent(node_id, parent_id)) {
    return;
  }

  TreeNodeData update;
  update.node_id = node.value(QStringLiteral("nodeId")).toString();
  update.parent_id = parent_id;
  update.type = static_cast<onerss::pb::TreeNode::Type>(node.value(QStringLiteral("type")).toInt());
  update.title = node.value(QStringLiteral("title")).toString();
  update.feed_url = node.value(QStringLiteral("feedUrl")).toString();
  update.comment = node.value(QStringLiteral("comment")).toString();
  update.use_default_refresh_interval = node.value(QStringLiteral("useDefaultRefreshInterval")).toBool();
  update.refresh_interval_hours = node.value(QStringLiteral("refreshIntervalHours")).toInt();

  if (!parent_id.isEmpty()) {
    feed_tree_model_.expandNode(parent_id);
  }

  runAsync([this, update]() {
    try {
      const auto moved = app_client_.updateNode(update);
      QMetaObject::invokeMethod(this, [this, moved]() { feed_tree_model_.upsertNode(moved); }, Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Move node failed: " << error.what();
    }
  });
}

void MainViewModel::selectNode(const QString &node_id) {
  if (selected_node_id_ == node_id) {
    return;
  }
  selected_node_id_ = node_id;
  emit selectedNodeIdChanged();
  reloadArticlesForCurrentNode();
}

void MainViewModel::selectArticleRow(const int row) {
  article_list_model_.selectRow(row);
  emit selectedArticleStateChanged();
  static_cast<void>(markSelectedArticleRead());
}

void MainViewModel::markAllArticlesRead(const QString &node_id) {
  if (node_id.isEmpty() || node_id == QStringLiteral("__root__")) {
    return;
  }
  const bool affects_loaded_node = selected_node_id_ == node_id;
  const auto local_unread = affects_loaded_node ? article_list_model_.unreadCount() : 0;
  if (affects_loaded_node && local_unread > 0) {
    article_list_model_.markAllRead();
    unread_count_ = std::max(0, unread_count_ - local_unread);
    emit unreadCountChanged();
  }
  runAsync([this, node_id]() {
    try {
      static_cast<void>(app_client_.markAllArticlesRead(node_id));
      refreshUnreadCount();
    } catch (const std::exception &error) {
      LOG_WARN << "Mark all articles read failed: " << error.what();
      refreshUnreadCount();
    }
  });
}

void MainViewModel::updateUserRefreshIntervalHours(const int hours) {
  runAsync([this, hours]() {
    try {
      const auto settings = app_client_.updateUserSettings(UserSettingsData{
        .default_refresh_interval_hours = std::max(1, hours),
      });
      QMetaObject::invokeMethod(this,
                                [this, settings]() {
                                  if (default_refresh_interval_hours_ != settings.default_refresh_interval_hours) {
                                    default_refresh_interval_hours_ = settings.default_refresh_interval_hours;
                                    emit userSettingsChanged();
                                  }
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Update user settings failed: " << error.what();
    }
  });
}

void MainViewModel::markSelectedArticleUnread() {
  const auto row = article_list_model_.selectedRow();
  const auto article = article_list_model_.articleAt(row);
  if (article.isEmpty() || !article.value(QStringLiteral("isRead")).toBool()) {
    return;
  }

  if (article_list_model_.markUnreadByRow(row)) {
    ++unread_count_;
    emit unreadCountChanged();
    emit selectedArticleStateChanged();
  }

  runAsync([this,
            node_id = article.value(QStringLiteral("nodeId")).toString(),
            article_id = article.value(QStringLiteral("articleId")).toString()]() {
    try {
      static_cast<void>(app_client_.markArticleUnread(node_id, article_id));
    } catch (const std::exception &error) {
      LOG_WARN << "Mark article unread failed: " << error.what();
      refreshUnreadCount();
      reloadArticlesForCurrentNode();
    }
  });
}

void MainViewModel::openSelectedArticle() {
  static_cast<void>(markSelectedArticleRead());
  if (selected_article_link_.isEmpty()) {
    return;
  }
  QDesktopServices::openUrl(QUrl{selected_article_link_});
}

void MainViewModel::clearStatusBar() {
  setStatusBarMessage(UiStatusMessage{});
}

void MainViewModel::requestFeedTitleLookup(const QString &feed_url) {
  const auto trimmed_url = feed_url.trimmed();
  if (trimmed_url.isEmpty()) {
    return;
  }

  runAsync([this, trimmed_url]() {
    const auto title = tryResolveFeedTitle(trimmed_url);
    if (!title.has_value()) {
      return;
    }
    QMetaObject::invokeMethod(this,
                              [this, trimmed_url, title = *title]() { emit feedTitleLookupFinished(trimmed_url, title); },
                              Qt::QueuedConnection);
  });
}

void MainViewModel::loadMoreArticles() {
  if (loading_more_articles_ || !can_load_more_articles_ || selected_node_id_ == QStringLiteral("__root__")) {
    return;
  }
  loadArticlesPage(selected_node_id_, true);
}

void MainViewModel::onPeerMaterialStored() {
  reconnect();
}

void MainViewModel::setConnectionStatus(const QString &value) {
  if (connection_status_ == value) {
    return;
  }
  connection_status_ = value;
  emit connectionStatusChanged();
}

void MainViewModel::runAsync(std::function<void()> work) {
  std::thread([work = std::move(work)]() mutable { work(); }).detach();
}

void MainViewModel::setStatusBarMessage(const UiStatusMessage &message) {
  status_bar_message_ = message;
  emit statusBarChanged();
}

void MainViewModel::reloadArticlesForCurrentNode() {
  LOG_TRACE << "Reloading articles for current node=" << selected_node_id_.toStdString();
  loadArticlesPage(selected_node_id_, false);
}

void MainViewModel::loadArticlesPage(const QString &node_id, const bool append) {
  if (loading_more_articles_) {
    return;
  }

  const auto request_node_id = node_id;
  const auto offset = append ? next_article_offset_ : 0;
  const auto limit = request_node_id == QStringLiteral("__root__") ? 1000 : 200;
  LOG_TRACE << "Requesting article page node_id=" << request_node_id.toStdString()
            << " append=" << append
            << " offset=" << offset
            << " limit=" << limit;
  if (!append) {
    next_article_offset_ = 0;
    can_load_more_articles_ = false;
  }
  loading_more_articles_ = true;
  emit articlePagingChanged();

  runAsync([this, request_node_id, append, offset, limit]() {
    try {
      auto page = app_client_.fetchArticles(request_node_id, offset, limit);
      page.articles = enrichArticles(page.articles);
      QMetaObject::invokeMethod(this,
                                [this, request_node_id, append, page]() {
                                  if (selected_node_id_ != request_node_id) {
                                    if (loading_more_articles_) {
                                      loading_more_articles_ = false;
                                      emit articlePagingChanged();
                                    }
                                    return;
                                  }

                                  if (!append) {
                                    article_list_model_.setArticles(page.articles);
                                    article_cache_.insert(request_node_id,
                                                          CachedArticlePage{
                                                            .articles = page.articles,
                                                            .has_more = page.has_more,
                                                            .next_offset = page.next_offset,
                                                          });
                                  } else {
                                    article_list_model_.appendArticles(page.articles);
                                    auto cached_page = article_cache_.value(request_node_id);
                                    for (const auto &article : page.articles) {
                                      cached_page.articles.push_back(article);
                                    }
                                    cached_page.has_more = page.has_more;
                                    cached_page.next_offset = page.next_offset;
                                    article_cache_.insert(request_node_id, std::move(cached_page));
                                  }
                                  LOG_TRACE << "Loaded article page node_id=" << request_node_id.toStdString()
                                            << " append=" << append
                                            << " fetched=" << page.articles.size()
                                            << " has_more=" << page.has_more
                                            << " next_offset=" << page.next_offset;
                                  loaded_articles_node_id_ = request_node_id;
                                  next_article_offset_ = page.next_offset;
                                  can_load_more_articles_ = request_node_id != QStringLiteral("__root__") && page.has_more;
                                  loading_more_articles_ = false;
                                  emit articlePagingChanged();
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Fetch articles failed: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, request_node_id, append, message = QString::fromUtf8(error.what())]() {
                                  if (selected_node_id_ != request_node_id) {
                                    return;
                                  }
                                  if (!append) {
                                    if (article_cache_.contains(request_node_id)) {
                                      const auto cached_page = article_cache_.value(request_node_id);
                                      article_list_model_.setArticles(cached_page.articles);
                                      next_article_offset_ = cached_page.next_offset;
                                      can_load_more_articles_
                                        = request_node_id != QStringLiteral("__root__") && cached_page.has_more;
                                      setStatusBarMessage(UiStatusMessage{
                                        .severity = UiStatusMessage::Severity::Warning,
                                        .text = tr("Showing cached articles for the selected feed."),
                                        .detail = message
                                      });
                                    } else {
                                      article_list_model_.setArticles({});
                                      next_article_offset_ = 0;
                                      can_load_more_articles_ = false;
                                      setStatusBarMessage(UiStatusMessage{
                                        .severity = UiStatusMessage::Severity::Error,
                                        .text = tr("Could not load articles for the selected feed."),
                                        .detail = message
                                      });
                                    }
                                  }
                                  loading_more_articles_ = false;
                                  if (append) {
                                    can_load_more_articles_ = false;
                                    setStatusBarMessage(UiStatusMessage{
                                      .severity = UiStatusMessage::Severity::Warning,
                                      .text = tr("Could not load more articles."),
                                      .detail = message
                                    });
                                  }
                                  emit articlePagingChanged();
                                  if (message.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                                      || message.contains(QStringLiteral("not connected"), Qt::CaseInsensitive)) {
                                    setConnectionStatus(tr("Disconnected"));
                                    scheduleReconnect(1000);
                                  }
                                },
                                Qt::QueuedConnection);
    }
  });
}

void MainViewModel::refreshUnreadCount() {
  runAsync([this]() {
    try {
      const auto unread = app_client_.fetchUnreadCount();
      QMetaObject::invokeMethod(this,
                                [this, unread]() {
                                  if (unread_count_ != unread) {
                                    unread_count_ = unread;
                                    emit unreadCountChanged();
                                  }
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Fetch unread count failed: " << error.what();
    }
  });
}

void MainViewModel::scheduleReconnect(const int delay_ms) {
  if (reconnect_in_progress_ || !isNetworkReachable()) {
    return;
  }
  if (delay_ms <= 0) {
    reconnect();
    return;
  }
  reconnect_timer_.start(delay_ms);
}

void MainViewModel::performHealthCheck() {
  if (connection_status_ != tr("Connected")) {
    return;
  }
  if (!isNetworkReachable()) {
    health_check_timer_.stop();
    setConnectionStatus(tr("Waiting for network"));
    setStatusBarMessage(UiStatusMessage{
      .severity = UiStatusMessage::Severity::Warning,
      .text = tr("Network unavailable."),
      .detail = tr("OneRSS will reconnect when connectivity returns.")
    });
    app_client_.stop();
    scheduleReconnect(60 * 1000);
    return;
  }

  runAsync([this]() {
    try {
      app_client_.ping();
    } catch (const std::exception &error) {
      LOG_WARN << "Health check ping failed: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, message = QString::fromUtf8(error.what())]() {
                                  setConnectionStatus(tr("Disconnected"));
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Warning,
                                    .text = tr("Server health check failed."),
                                    .detail = message
                                  });
                                  app_client_.stop();
                                  scheduleReconnect(1000);
                                },
                                Qt::QueuedConnection);
    }
  });
}

bool MainViewModel::isNetworkReachable() const {
  const auto *network_information = QNetworkInformation::instance();
  if (network_information == nullptr) {
    return true;
  }

  switch (network_information->reachability()) {
    case QNetworkInformation::Reachability::Disconnected:
      return false;
    case QNetworkInformation::Reachability::Unknown:
      return true;
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
    case QNetworkInformation::Reachability::Online:
      return true;
  }

  return true;
}

void MainViewModel::handleNetworkReachabilityChanged() {
  if (isNetworkReachable()) {
    if (connection_status_ != tr("Connected")) {
      scheduleReconnect(1000);
    }
    return;
  }

  health_check_timer_.stop();
  setConnectionStatus(tr("Waiting for network"));
  setStatusBarMessage(UiStatusMessage{
    .severity = UiStatusMessage::Severity::Warning,
    .text = tr("Network unavailable."),
    .detail = tr("OneRSS will reconnect when connectivity returns.")
  });
  app_client_.stop();
}

QVector<ArticleData> MainViewModel::enrichArticles(const QVector<ArticleData> &articles) const {
  QVector<ArticleData> enriched = articles;
  for (auto &article : enriched) {
    article.feed_title = feed_tree_model_.titleForNode(article.node_id);
  }
  return enriched;
}

bool MainViewModel::markSelectedArticleRead() {
  const auto row = article_list_model_.selectedRow();
  const auto article = article_list_model_.articleAt(row);
  if (article.isEmpty() || article.value(QStringLiteral("isRead")).toBool()) {
    return false;
  }

  if (article_list_model_.markReadByRow(row) && unread_count_ > 0) {
    --unread_count_;
    emit unreadCountChanged();
    emit selectedArticleStateChanged();
  }

  runAsync([this,
            node_id = article.value(QStringLiteral("nodeId")).toString(),
            article_id = article.value(QStringLiteral("articleId")).toString()]() {
    try {
      static_cast<void>(app_client_.markArticleRead(node_id, article_id));
    } catch (const std::exception &error) {
      LOG_WARN << "Mark article read failed: " << error.what();
      refreshUnreadCount();
      reloadArticlesForCurrentNode();
    }
  });
  return true;
}

void MainViewModel::setPreviewFromArticle(const QVariantMap &article) {
  preview_title_ = article.value(QStringLiteral("title")).toString();
  const auto author = article.value(QStringLiteral("author")).toString();
  const auto published = article.value(QStringLiteral("publishedAt")).toString();
  preview_meta_ = author.isEmpty() ? published : tr("%1 | %2").arg(author, published);
  preview_content_ = article.value(QStringLiteral("content")).toString();
  selected_article_link_ = article.value(QStringLiteral("linkUrl")).toString();
  emit previewChanged();
  emit selectedArticleStateChanged();
}

}  // namespace onerss::desktop
