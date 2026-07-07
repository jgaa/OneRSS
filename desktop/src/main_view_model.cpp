#include "feed_title_resolver.h"
#include "main_view_model.h"

#include "logging.h"

#include <QNetworkInformation>
#include <QNetworkInterface>
#include <QMetaObject>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSet>
#include <algorithm>
#include <functional>
#include <typeinfo>

#ifndef __ANDROID__
#  include <QApplication>
#  include <QFileDialog>
#  include <QMessageBox>
#  include <QPushButton>
#  include <QWidget>
#endif

#include <thread>

namespace onerss::desktop {
namespace {

bool isContainerInterface(const QString &interface_name) {
  const auto normalized = interface_name.trimmed().toLower();
  return normalized.startsWith(QStringLiteral("docker"))
         || normalized.startsWith(QStringLiteral("br-"))
         || normalized.startsWith(QStringLiteral("veth"))
         || normalized.startsWith(QStringLiteral("cni"))
         || normalized.startsWith(QStringLiteral("podman"));
}

bool isUsableNetworkAddress(const QNetworkInterface &interface, const QHostAddress &address) {
  if (!(interface.flags().testFlag(QNetworkInterface::IsUp) && interface.flags().testFlag(QNetworkInterface::IsRunning))
      || interface.flags().testFlag(QNetworkInterface::IsLoopBack)
      || isContainerInterface(interface.name())
      || address.isNull()
      || address.isLoopback()
      || address.isMulticast()) {
    return false;
  }

  const auto protocol = address.protocol();
  if (protocol != QAbstractSocket::IPv4Protocol && protocol != QAbstractSocket::IPv6Protocol) {
    return false;
  }

  const auto text = address.toString().trimmed().toLower();
  if (text.isEmpty()) {
    return false;
  }

  if (protocol == QAbstractSocket::IPv4Protocol) {
    return !text.startsWith(QStringLiteral("169.254."));
  }

  return !text.startsWith(QStringLiteral("fe80:"));
}

struct OpmlOutline {
  QString title;
  QString xml_url;
  QString html_url;
  QString comment;
  QVector<OpmlOutline> children;

  [[nodiscard]] bool isFeed() const {
    return !xml_url.trimmed().isEmpty();
  }

  [[nodiscard]] bool hasFeedDescendants() const {
    if (isFeed()) {
      return true;
    }
    return std::any_of(children.cbegin(), children.cend(), [](const auto &child) {
      return child.hasFeedDescendants();
    });
  }
};

QString normalizedTitle(const QString &value) {
  return value.trimmed().toLower();
}

QString normalizedFeedUrl(const QString &value) {
  return value.trimmed();
}

OpmlOutline parseOpmlOutline(QXmlStreamReader &xml) {
  OpmlOutline outline;
  const auto attributes = xml.attributes();
  outline.title = attributes.value(QStringLiteral("text")).toString().trimmed();
  if (outline.title.isEmpty()) {
    outline.title = attributes.value(QStringLiteral("title")).toString().trimmed();
  }
  outline.xml_url = attributes.value(QStringLiteral("xmlUrl")).toString().trimmed();
  outline.html_url = attributes.value(QStringLiteral("htmlUrl")).toString().trimmed();
  outline.comment = attributes.value(QStringLiteral("comment")).toString().trimmed();

  while (xml.readNextStartElement()) {
    if (xml.name() == QStringLiteral("outline")) {
      outline.children.push_back(parseOpmlOutline(xml));
    } else {
      xml.skipCurrentElement();
    }
  }

  return outline;
}

QVector<OpmlOutline> parseOpmlDocument(const QString &path) {
  QFile file{path};
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    throw std::runtime_error{QStringLiteral("Could not open %1").arg(path).toStdString()};
  }

  QXmlStreamReader xml{&file};
  QVector<OpmlOutline> outlines;
  while (xml.readNextStartElement()) {
    if (xml.name() != QStringLiteral("opml")) {
      xml.skipCurrentElement();
      continue;
    }

    while (xml.readNextStartElement()) {
      if (xml.name() != QStringLiteral("body")) {
        xml.skipCurrentElement();
        continue;
      }

      while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("outline")) {
          outlines.push_back(parseOpmlOutline(xml));
        } else {
          xml.skipCurrentElement();
        }
      }
    }
  }

  if (xml.hasError()) {
    throw std::runtime_error{QStringLiteral("Failed to parse OPML: %1").arg(xml.errorString()).toStdString()};
  }

  return outlines;
}

void sortNodesFolderFirst(QVector<TreeNodeData> &nodes) {
  std::sort(nodes.begin(), nodes.end(), [](const auto &lhs, const auto &rhs) {
    if (lhs.type != rhs.type) {
      return lhs.type < rhs.type;
    }
    const auto title_compare = lhs.title.localeAwareCompare(rhs.title);
    if (title_compare != 0) {
      return title_compare < 0;
    }
    return lhs.node_id < rhs.node_id;
  });
}

int nodeDepth(const QString &node_id, const QHash<QString, TreeNodeData> &nodes_by_id) {
  int depth = 0;
  auto current_id = node_id;
  while (!current_id.isEmpty()) {
    const auto it = nodes_by_id.find(current_id);
    if (it == nodes_by_id.end() || it->parent_id.isEmpty()) {
      break;
    }
    ++depth;
    current_id = it->parent_id;
  }
  return depth;
}

TreeNodeData makeFeedNode(const QString &parent_id, const OpmlOutline &outline) {
  TreeNodeData node;
  node.parent_id = parent_id;
  node.type = onerss::pb::TreeNode::Type::TYPE_FEED;
  node.title = outline.title.isEmpty() ? outline.xml_url : outline.title;
  node.feed_url = outline.xml_url;
  node.comment = outline.comment;
  return node;
}

void writeOpmlOutline(QXmlStreamWriter &xml,
                      const QString &parent_id,
                      const QHash<QString, QVector<TreeNodeData>> &children_by_parent) {
  auto children = children_by_parent.value(parent_id);
  sortNodesFolderFirst(children);
  for (const auto &child : children) {
    xml.writeStartElement(QStringLiteral("outline"));
    xml.writeAttribute(QStringLiteral("text"), child.title);
    if (child.type == onerss::pb::TreeNode::Type::TYPE_FOLDER) {
      xml.writeAttribute(QStringLiteral("isOpen"), QStringLiteral("false"));
      writeOpmlOutline(xml, child.node_id, children_by_parent);
    } else {
      xml.writeAttribute(QStringLiteral("title"), child.title);
      xml.writeAttribute(QStringLiteral("type"), QStringLiteral("rss"));
      xml.writeAttribute(QStringLiteral("xmlUrl"), child.feed_url);
      xml.writeAttribute(QStringLiteral("htmlUrl"), QStringLiteral(""));
      xml.writeAttribute(QStringLiteral("comment"), child.comment);
      xml.writeAttribute(QStringLiteral("version"), QStringLiteral("RSS"));
    }
    xml.writeEndElement();
  }
}

}  // namespace

MainViewModel::MainViewModel(QObject *parent) : QObject(parent) {
  health_check_timer_.setInterval(60 * 1000);
  connect(&health_check_timer_, &QTimer::timeout, this, [this]() { performHealthCheck(); });

  network_change_timer_.setInterval(3000);
  connect(&network_change_timer_, &QTimer::timeout, this, [this]() { handleNetworkEnvironmentChanged(); });
  network_change_timer_.start();

  reconnect_timer_.setSingleShot(true);
  connect(&reconnect_timer_, &QTimer::timeout, this, [this]() { reconnect(); });

  static_cast<void>(QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability));
  if (auto *network_information = QNetworkInformation::instance()) {
    connect(network_information,
            &QNetworkInformation::reachabilityChanged,
            this,
            [this](QNetworkInformation::Reachability) { handleNetworkReachabilityChanged(); });
  }
  last_network_environment_fingerprint_ = currentNetworkEnvironmentFingerprint();

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
                                if (node_id.isEmpty()) {
                                  LOG_TRACE << "Clearing cached article pages after broad notification";
                                  article_cache_.clear();
                                }
                                if (node_id.isEmpty()
                                    || selected_node_id_ == QStringLiteral("__root__")
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
                                if (!isNetworkReachable()) {
                                  stopForNetworkLoss();
                                } else if (errorSuggestsServerRouteUnavailable(reason)) {
                                  deferReconnectUntilNetworkChanges(reason);
                                } else {
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

QString MainViewModel::articleSearchQuery() const {
  return article_search_query_;
}

bool MainViewModel::showingReadArticles() const {
  return showing_read_articles_;
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
                                    stopForNetworkLoss();
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
                                  waiting_for_network_change_ = false;
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
                                  if (!isNetworkReachable()) {
                                    stopForNetworkLoss();
                                    return;
                                  }
                                  setConnectionStatus(tr("Connection failed: %1").arg(message));
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Error,
                                    .text = tr("Could not connect to the server."),
                                    .detail = message
                                  });
                                  if (errorSuggestsServerRouteUnavailable(message)) {
                                    deferReconnectUntilNetworkChanges(message);
                                  } else {
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
  if (node_id.isEmpty()) {
    return;
  }
  const bool affects_loaded_node = selected_node_id_ == node_id
                                   || (node_id == QStringLiteral("__root__")
                                       && selected_node_id_ == QStringLiteral("__root__"));
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
  if (loading_more_articles_ || !can_load_more_articles_) {
    return;
  }
  loadArticlesPage(selected_node_id_, true);
}

void MainViewModel::setFeedFilterText(const QString &text) {
  feed_tree_model_.setFilterText(text);
}

void MainViewModel::searchArticles(const QString &query) {
  const auto normalized = query.trimmed();
  if (article_search_query_ == normalized && !loaded_articles_node_id_.isEmpty()) {
    reloadArticlesForCurrentNode();
    return;
  }
  article_search_query_ = normalized;
  emit articleSearchChanged();
  reloadArticlesForCurrentNode();
}

void MainViewModel::setShowingReadArticles(const bool value) {
  if (showing_read_articles_ == value) {
    return;
  }
  showing_read_articles_ = value;
  emit articleFilterChanged();
  reloadArticlesForCurrentNode();
}

void MainViewModel::toggleShowingReadArticles() {
  setShowingReadArticles(!showing_read_articles_);
}

void MainViewModel::copyArticleUrl(const int row) {
  const auto article = article_list_model_.articleAt(row);
  const auto link_url = article.value(QStringLiteral("linkUrl")).toString().trimmed();
  if (link_url.isEmpty()) {
    return;
  }

  if (auto *clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(link_url, QClipboard::Clipboard);
    clipboard->setText(link_url, QClipboard::Selection);
  }
}

void MainViewModel::importFeeds() {
#ifdef __ANDROID__
  setStatusBarMessage(UiStatusMessage{
    .severity = UiStatusMessage::Severity::Warning,
    .text = tr("Feed import is not available on this platform."),
  });
  return;
#else
  QWidget *parent_widget = QApplication::activeWindow();
  const auto file_path = QFileDialog::getOpenFileName(parent_widget,
                                                      tr("Import Feeds"),
                                                      QString{},
                                                      tr("OPML Files (*.opml *.xml);;All Files (*)"));
  if (file_path.isEmpty()) {
    return;
  }

  QMessageBox choice_box{parent_widget};
  choice_box.setWindowTitle(tr("Import Feeds"));
  choice_box.setText(tr("How should imported feeds be applied?"));
  choice_box.setInformativeText(tr("Merge preserves existing feeds. Overwrite replaces the current feed tree."));
  auto *merge_button = choice_box.addButton(tr("Merge"), QMessageBox::AcceptRole);
  auto *overwrite_button = choice_box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
  choice_box.addButton(QMessageBox::Cancel);
  choice_box.setDefaultButton(qobject_cast<QPushButton *>(merge_button));
  choice_box.exec();

  if (choice_box.clickedButton() != merge_button && choice_box.clickedButton() != overwrite_button) {
    return;
  }
  const bool overwrite = choice_box.clickedButton() == overwrite_button;

  runAsync([this, file_path, overwrite]() {
    try {
      const auto outlines = parseOpmlDocument(file_path);
      auto existing_nodes = app_client_.fetchTree();

      int created_folder_count = 0;
      int created_feed_count = 0;

      if (overwrite) {
        QHash<QString, TreeNodeData> nodes_by_id;
        for (const auto &node : existing_nodes) {
          nodes_by_id.insert(node.node_id, node);
        }
        std::sort(existing_nodes.begin(), existing_nodes.end(), [&nodes_by_id](const auto &lhs, const auto &rhs) {
          const auto lhs_depth = nodeDepth(lhs.node_id, nodes_by_id);
          const auto rhs_depth = nodeDepth(rhs.node_id, nodes_by_id);
          if (lhs_depth != rhs_depth) {
            return lhs_depth > rhs_depth;
          }
          return lhs.node_id < rhs.node_id;
        });
        for (const auto &node : existing_nodes) {
          app_client_.deleteNode(node.node_id);
        }
        existing_nodes.clear();
      }

      std::function<void(const QVector<OpmlOutline> &, const QString &, bool)> import_outlines;
      import_outlines = [&](const QVector<OpmlOutline> &outline_list, const QString &parent_id, const bool merge_mode) {
        auto child_nodes = existing_nodes;
        QVector<TreeNodeData> children;
        for (const auto &node : child_nodes) {
          if (node.parent_id == parent_id) {
            children.push_back(node);
          }
        }

        for (const auto &outline : outline_list) {
          if (outline.isFeed()) {
            const auto xml_url = normalizedFeedUrl(outline.xml_url);
            if (xml_url.isEmpty()) {
              continue;
            }

            const auto feed_exists = std::any_of(children.cbegin(), children.cend(), [&xml_url](const auto &node) {
              return node.type == onerss::pb::TreeNode::Type::TYPE_FEED
                     && normalizedFeedUrl(node.feed_url) == xml_url;
            });
            if (merge_mode && feed_exists) {
              continue;
            }

            const auto created = app_client_.createFeed(parent_id,
                                                        outline.title.isEmpty() ? outline.xml_url : outline.title,
                                                        outline.xml_url,
                                                        outline.comment);
            existing_nodes.push_back(created);
            children.push_back(created);
            ++created_feed_count;
            continue;
          }

          if (merge_mode && !outline.hasFeedDescendants()) {
            continue;
          }

          TreeNodeData matching_folder;
          bool has_matching_folder = false;
          for (const auto &node : children) {
            if (node.type == onerss::pb::TreeNode::Type::TYPE_FOLDER
                && normalizedTitle(node.title) == normalizedTitle(outline.title)) {
              matching_folder = node;
              has_matching_folder = true;
              break;
            }
          }

          if (has_matching_folder) {
            import_outlines(outline.children, matching_folder.node_id, merge_mode);
            continue;
          }

          if (merge_mode && !outline.hasFeedDescendants()) {
            continue;
          }

          const auto created_folder = app_client_.createFolder(parent_id, outline.title);
          existing_nodes.push_back(created_folder);
          children.push_back(created_folder);
          ++created_folder_count;
          import_outlines(outline.children, created_folder.node_id, merge_mode);
        }
      };

      import_outlines(outlines, QString{}, !overwrite);

      const auto nodes = app_client_.fetchTree();
      const auto unread_count = app_client_.fetchUnreadCount();
      QMetaObject::invokeMethod(this,
                                [this, nodes, unread_count, created_folder_count, created_feed_count, overwrite]() {
                                  feed_tree_model_.loadNodes(nodes);
                                  if (unread_count_ != unread_count) {
                                    unread_count_ = unread_count;
                                    emit unreadCountChanged();
                                  }

                                  const auto selected_exists = std::any_of(nodes.cbegin(), nodes.cend(), [this](const auto &node) {
                                    return node.node_id == selected_node_id_;
                                  });
                                  if (selected_node_id_ != QStringLiteral("__root__") && !selected_exists) {
                                    selected_node_id_ = QStringLiteral("__root__");
                                    emit selectedNodeIdChanged();
                                  }

                                  reloadArticlesForCurrentNode();
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Success,
                                    .text = overwrite ? tr("Feeds imported with overwrite.") : tr("Feeds merged from OPML."),
                                    .detail = tr("Created %1 folders and %2 feeds.")
                                                .arg(created_folder_count)
                                                .arg(created_feed_count)
                                  });
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Import feeds failed: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, message = QString::fromUtf8(error.what())]() {
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Error,
                                    .text = tr("Could not import feeds."),
                                    .detail = message
                                  });
                                },
                                Qt::QueuedConnection);
    }
  });
#endif
}

void MainViewModel::exportFeeds() {
#ifdef __ANDROID__
  setStatusBarMessage(UiStatusMessage{
    .severity = UiStatusMessage::Severity::Warning,
    .text = tr("Feed export is not available on this platform."),
  });
  return;
#else
  QWidget *parent_widget = QApplication::activeWindow();
  const auto file_path = QFileDialog::getSaveFileName(parent_widget,
                                                      tr("Export Feeds"),
                                                      QStringLiteral("feeds.opml"),
                                                      tr("OPML Files (*.opml);;XML Files (*.xml);;All Files (*)"));
  if (file_path.isEmpty()) {
    return;
  }

  runAsync([this, file_path]() {
    try {
      auto nodes = app_client_.fetchTree();
      QHash<QString, QVector<TreeNodeData>> children_by_parent;
      for (const auto &node : nodes) {
        children_by_parent[node.parent_id].push_back(node);
      }

      QSaveFile file{file_path};
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error{QStringLiteral("Could not open %1 for writing").arg(file_path).toStdString()};
      }

      QXmlStreamWriter xml{&file};
      xml.setAutoFormatting(true);
      xml.writeStartDocument();
      xml.writeStartElement(QStringLiteral("opml"));
      xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
      xml.writeStartElement(QStringLiteral("head"));
      xml.writeTextElement(QStringLiteral("title"), tr("OneRSS Feeds"));
      xml.writeEndElement();
      xml.writeStartElement(QStringLiteral("body"));
      writeOpmlOutline(xml, QString{}, children_by_parent);
      xml.writeEndElement();
      xml.writeEndElement();
      xml.writeEndDocument();

      if (!file.commit()) {
        throw std::runtime_error{QStringLiteral("Could not save %1").arg(file_path).toStdString()};
      }

      QMetaObject::invokeMethod(this,
                                [this, file_path]() {
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Success,
                                    .text = tr("Feeds exported."),
                                    .detail = QFileInfo{file_path}.fileName()
                                  });
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Export feeds failed: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, message = QString::fromUtf8(error.what())]() {
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Error,
                                    .text = tr("Could not export feeds."),
                                    .detail = message
                                  });
                                },
                                Qt::QueuedConnection);
    }
  });
#endif
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
  const auto request_query = article_search_query_;
  const auto unread_only = !showing_read_articles_;
  const auto cache_key
    = request_node_id + QStringLiteral("\n") + request_query + QStringLiteral("\n") + (unread_only ? QStringLiteral("1") : QStringLiteral("0"));
  const auto offset = append ? next_article_offset_ : 0;
  const auto limit = 200;
  LOG_TRACE << "Requesting article page node_id=" << request_node_id.toStdString()
            << " title_query=" << request_query.toStdString()
            << " unread_only=" << unread_only
            << " append=" << append
            << " offset=" << offset
            << " limit=" << limit;
  if (!append) {
    next_article_offset_ = 0;
    can_load_more_articles_ = false;
  }
  loading_more_articles_ = true;
  emit articlePagingChanged();

  runAsync([this, request_node_id, request_query, unread_only, cache_key, append, offset, limit]() {
    try {
      auto page = app_client_.fetchArticles(request_node_id, request_query, unread_only, offset, limit);
      page.articles = enrichArticles(page.articles);
      QMetaObject::invokeMethod(this,
                                [this, request_node_id, request_query, unread_only, cache_key, append, page]() {
                                  if (selected_node_id_ != request_node_id
                                      || article_search_query_ != request_query
                                      || !showing_read_articles_ != unread_only) {
                                    if (loading_more_articles_) {
                                      loading_more_articles_ = false;
                                      emit articlePagingChanged();
                                    }
                                    return;
                                  }

                                  if (!append) {
                                    article_list_model_.setArticles(page.articles);
                                    article_cache_.insert(cache_key,
                                                          CachedArticlePage{
                                                            .articles = page.articles,
                                                            .has_more = page.has_more,
                                                            .next_offset = page.next_offset,
                                                          });
                                  } else {
                                    article_list_model_.appendArticles(page.articles);
                                    auto cached_page = article_cache_.value(cache_key);
                                    for (const auto &article : page.articles) {
                                      cached_page.articles.push_back(article);
                                    }
                                    cached_page.has_more = page.has_more;
                                    cached_page.next_offset = page.next_offset;
                                    article_cache_.insert(cache_key, std::move(cached_page));
                                  }
                                  LOG_TRACE << "Loaded article page node_id=" << request_node_id.toStdString()
                                            << " title_query=" << request_query.toStdString()
                                            << " append=" << append
                                            << " fetched=" << page.articles.size()
                                            << " has_more=" << page.has_more
                                            << " next_offset=" << page.next_offset;
                                  loaded_articles_node_id_ = request_node_id;
                                  next_article_offset_ = page.next_offset;
                                  can_load_more_articles_ = page.has_more;
                                  loading_more_articles_ = false;
                                  emit articlePagingChanged();
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Fetch articles failed: " << error.what();
      QMetaObject::invokeMethod(this,
                                [this, request_node_id, request_query, unread_only, cache_key, append, message = QString::fromUtf8(error.what())]() {
                                  if (selected_node_id_ != request_node_id
                                      || article_search_query_ != request_query
                                      || !showing_read_articles_ != unread_only) {
                                    return;
                                  }
                                  if (!append) {
                                    if (article_cache_.contains(cache_key)) {
                                      const auto cached_page = article_cache_.value(cache_key);
                                      article_list_model_.setArticles(cached_page.articles);
                                      next_article_offset_ = cached_page.next_offset;
                                      can_load_more_articles_ = cached_page.has_more;
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
  if (reconnect_in_progress_ || !isNetworkReachable() || waiting_for_network_change_) {
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
    stopForNetworkLoss();
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
                                  app_client_.stop();
                                  if (!isNetworkReachable()) {
                                    stopForNetworkLoss();
                                    return;
                                  }
                                  setStatusBarMessage(UiStatusMessage{
                                    .severity = UiStatusMessage::Severity::Warning,
                                    .text = tr("Server health check failed."),
                                    .detail = message
                                  });
                                  if (errorSuggestsServerRouteUnavailable(message)) {
                                    deferReconnectUntilNetworkChanges(message);
                                  } else {
                                    scheduleReconnect(1000);
                                  }
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
  handleNetworkEnvironmentChanged();
  if (isNetworkReachable()) {
    if (connection_status_ != tr("Connected") && !waiting_for_network_change_) {
      scheduleReconnect(1000);
    }
    return;
  }

  stopForNetworkLoss();
}

void MainViewModel::handleNetworkEnvironmentChanged() {
  const auto current_fingerprint = currentNetworkEnvironmentFingerprint();
  if (current_fingerprint == last_network_environment_fingerprint_) {
    return;
  }

  last_network_environment_fingerprint_ = current_fingerprint;
  LOG_INFO << "Detected network environment change";

  const bool was_waiting_for_network_change = waiting_for_network_change_;
  waiting_for_network_change_ = false;

  if (!isNetworkReachable()) {
    stopForNetworkLoss();
    return;
  }

  reconnect_timer_.stop();
  if (connection_status_ == tr("Connected")) {
    health_check_timer_.stop();
    app_client_.stop();
    setConnectionStatus(tr("Disconnected"));
  }

  if (was_waiting_for_network_change || connection_status_ != tr("Connected")) {
    scheduleReconnect(1000);
  }
}

void MainViewModel::stopForNetworkLoss() {
  waiting_for_network_change_ = false;
  reconnect_timer_.stop();
  health_check_timer_.stop();
  setConnectionStatus(tr("Waiting for network"));
  setStatusBarMessage(UiStatusMessage{
    .severity = UiStatusMessage::Severity::Warning,
    .text = tr("Network unavailable."),
    .detail = tr("OneRSS will reconnect when connectivity returns.")
  });
  app_client_.stop();
}

void MainViewModel::deferReconnectUntilNetworkChanges(const QString &detail) {
  waiting_for_network_change_ = true;
  reconnect_timer_.stop();
  health_check_timer_.stop();
  setConnectionStatus(tr("Waiting for network change"));
  setStatusBarMessage(UiStatusMessage{
    .severity = UiStatusMessage::Severity::Warning,
    .text = tr("Server is unreachable on the current network."),
    .detail = detail.isEmpty() ? tr("OneRSS will retry after the network changes.") : detail
  });
}

QString MainViewModel::currentNetworkEnvironmentFingerprint() const {
  QSet<QString> tokens;
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : interfaces) {
    for (const auto &entry : interface.addressEntries()) {
      if (!isUsableNetworkAddress(interface, entry.ip())) {
        continue;
      }
      tokens.insert(QStringLiteral("%1|%2").arg(interface.name(), entry.ip().toString().trimmed().toLower()));
    }
  }

  QStringList sorted_tokens = tokens.values();
  std::sort(sorted_tokens.begin(), sorted_tokens.end());
  return sorted_tokens.join(QStringLiteral(";"));
}

bool MainViewModel::errorSuggestsServerRouteUnavailable(const QString &message) const {
  const auto normalized = message.trimmed().toLower();
  return normalized.contains(QStringLiteral("no route to host"))
         || normalized.contains(QStringLiteral("host unreachable"))
         || normalized.contains(QStringLiteral("network unreachable"))
         || normalized.contains(QStringLiteral("host not found"))
         || normalized.contains(QStringLiteral("network operation timed out"))
         || normalized.contains(QStringLiteral("connection timed out"))
         || normalized.contains(QStringLiteral("request timed out"));
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
