#pragma once

#include "article_types.h"
#include "user_notification.h"
#include "secret_store.h"
#include "protocol_io.h"
#include "tree_types.h"

#include <QSslSocket>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <QVector>

namespace onerss::desktop {

struct ArticlePage {
  QVector<ArticleData> articles;
  bool has_more = false;
  int next_offset = 0;
};

struct UserSettingsData {
  int default_refresh_interval_hours = 12;
};

struct AppHelloData {
  QString user_id;
  QString login;
  QString device_id;
  QString server_version;
  QString database_name;
  QString database_version;
};

class AppClient final {
 public:
  AppClient();
  ~AppClient();

  [[nodiscard]] AppHelloData connectAndStart(const StoredPeer &peer);
  void stop();
  [[nodiscard]] QVector<TreeNodeData> fetchTree();
  [[nodiscard]] UserSettingsData fetchUserSettings();
  [[nodiscard]] UserSettingsData updateUserSettings(const UserSettingsData &settings);
  [[nodiscard]] int fetchUnreadCount();
  [[nodiscard]] ArticlePage fetchArticles(const QString &node_id,
                                          const QString &title_query,
                                          bool unread_only,
                                          int offset,
                                          int limit);
  void ping();
  [[nodiscard]] int markArticleRead(const QString &node_id, const QString &article_id);
  [[nodiscard]] int markArticleUnread(const QString &node_id, const QString &article_id);
  [[nodiscard]] int markAllArticlesRead(const QString &node_id);
  [[nodiscard]] TreeNodeData createFolder(const QString &parent_id, const QString &title);
  [[nodiscard]] TreeNodeData createFeed(const QString &parent_id,
                                        const QString &title,
                                        const QString &feed_url,
                                        const QString &comment);
  [[nodiscard]] TreeNodeData updateNode(const TreeNodeData &node);
  void deleteNode(const QString &node_id);
  void refreshFeed(const QString &node_id);

  std::function<void(const TreeNodeData &, const QString &origin_device_id)> onNodeUpserted;
  std::function<void(const QString &node_id, const QString &origin_device_id)> onNodeDeleted;
  std::function<void(const QString &node_id, const QString &origin_device_id)> onArticlesUpdated;
  std::function<void(const UserSettingsData &, const QString &origin_device_id)> onUserSettingsUpdated;
  std::function<void(const UiStatusMessage &message)> onUserNotification;
  std::function<void(const QString &reason)> onConnectionLost;

  [[nodiscard]] QString deviceId() const;

 private:
  [[nodiscard]] onerss::pb::OutgoingEnvelope request(const onerss::pb::IncomingEnvelope &request);
  void emitUserNotification(const onerss::pb::OutgoingEnvelope &envelope);
  void failPendingRequests(const QString &reason);
  void ioLoop(std::promise<void> ready_promise);
  void handleEnvelope(onerss::pb::OutgoingEnvelope envelope);
  static std::string newRequestId();

  StoredPeer peer_;
  std::thread io_thread_;
  std::mutex pending_mutex_;
  std::unordered_map<std::string, std::promise<onerss::pb::OutgoingEnvelope>> pending_;
  OutgoingMessageQueue<onerss::pb::IncomingEnvelope> outgoing_;
  std::atomic_bool running_ = false;
};

}  // namespace onerss::desktop
