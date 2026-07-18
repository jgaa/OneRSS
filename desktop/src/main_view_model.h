#pragma once

#include "browser_profile_manager.h"
#include "article_list_model.h"
#include "app_client.h"
#include "feed_tree_model.h"
#include "secret_store.h"
#include "user_notification.h"

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QTimer>

namespace onerss::desktop {

class MainViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int connectionState READ connectionState NOTIFY connectionStatusChanged)
  Q_PROPERTY(FeedTreeModel *feedTreeModel READ feedTreeModel CONSTANT)
  Q_PROPERTY(ArticleListModel *articleListModel READ articleListModel CONSTANT)
  Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
  Q_PROPERTY(QString statusBarText READ statusBarText NOTIFY statusBarChanged)
  Q_PROPERTY(QString statusBarDetail READ statusBarDetail NOTIFY statusBarChanged)
  Q_PROPERTY(int statusBarSeverity READ statusBarSeverity NOTIFY statusBarChanged)
  Q_PROPERTY(bool hasStatusBarDetail READ hasStatusBarDetail NOTIFY statusBarChanged)
  Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY selectedNodeIdChanged)
  Q_PROPERTY(int defaultRefreshIntervalHours READ defaultRefreshIntervalHours NOTIFY userSettingsChanged)
  Q_PROPERTY(int defaultArchiveMode READ defaultArchiveMode NOTIFY userSettingsChanged)
  Q_PROPERTY(int defaultArchiveLimit READ defaultArchiveLimit NOTIFY userSettingsChanged)
  Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
  Q_PROPERTY(QString previewTitle READ previewTitle NOTIFY previewChanged)
  Q_PROPERTY(QString previewMeta READ previewMeta NOTIFY previewChanged)
  Q_PROPERTY(QString previewContent READ previewContent NOTIFY previewChanged)
  Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
  Q_PROPERTY(bool selectedArticleIsRead READ selectedArticleIsRead NOTIFY selectedArticleStateChanged)
  Q_PROPERTY(bool selectedArticleIsQueued READ selectedArticleIsQueued NOTIFY selectedArticleStateChanged)
  Q_PROPERTY(bool canLoadMoreArticles READ canLoadMoreArticles NOTIFY articlePagingChanged)
  Q_PROPERTY(bool loadingMoreArticles READ loadingMoreArticles NOTIFY articlePagingChanged)
  Q_PROPERTY(QString articleSearchQuery READ articleSearchQuery NOTIFY articleSearchChanged)
  Q_PROPERTY(bool showingReadArticles READ showingReadArticles NOTIFY articleFilterChanged)
  Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverInfoChanged)
  Q_PROPERTY(QString serverDatabaseName READ serverDatabaseName NOTIFY serverInfoChanged)
  Q_PROPERTY(QString serverDatabaseVersion READ serverDatabaseVersion NOTIFY serverInfoChanged)
  Q_PROPERTY(bool hasServerInfo READ hasServerInfo NOTIFY serverInfoChanged)
  Q_PROPERTY(BrowserProfileManager *browserProfileManager READ browserProfileManager CONSTANT)

 public:
  explicit MainViewModel(QObject *parent = nullptr);
  ~MainViewModel() override;

  [[nodiscard]] FeedTreeModel *feedTreeModel();
  [[nodiscard]] ArticleListModel *articleListModel();
  [[nodiscard]] int connectionState() const;
  [[nodiscard]] QString connectionStatus() const;
  [[nodiscard]] QString statusBarText() const;
  [[nodiscard]] QString statusBarDetail() const;
  [[nodiscard]] int statusBarSeverity() const;
  [[nodiscard]] bool hasStatusBarDetail() const;
  [[nodiscard]] QString selectedNodeId() const;
  [[nodiscard]] int defaultRefreshIntervalHours() const;
  [[nodiscard]] int defaultArchiveMode() const;
  [[nodiscard]] int defaultArchiveLimit() const;
  [[nodiscard]] int unreadCount() const;
  [[nodiscard]] QString previewTitle() const;
  [[nodiscard]] QString previewMeta() const;
  [[nodiscard]] QString previewContent() const;
  [[nodiscard]] bool hasPreview() const;
  [[nodiscard]] bool selectedArticleIsRead() const;
  [[nodiscard]] bool selectedArticleIsQueued() const;
  [[nodiscard]] bool canLoadMoreArticles() const;
  [[nodiscard]] bool loadingMoreArticles() const;
  [[nodiscard]] QString articleSearchQuery() const;
  [[nodiscard]] bool showingReadArticles() const;
  [[nodiscard]] QString serverVersion() const;
  [[nodiscard]] QString serverDatabaseName() const;
  [[nodiscard]] QString serverDatabaseVersion() const;
  [[nodiscard]] bool hasServerInfo() const;
  [[nodiscard]] BrowserProfileManager *browserProfileManager();

  Q_INVOKABLE void reconnect();
  Q_INVOKABLE QVariantMap nodeData(const QString &node_id) const;
  Q_INVOKABLE void toggleExpanded(const QString &node_id);
  Q_INVOKABLE void addFolder(const QString &parent_id, const QString &title);
  Q_INVOKABLE void addFeed(const QString &parent_id,
                           const QString &title,
                           const QString &feed_url,
                           const QString &comment);
  Q_INVOKABLE void renameNode(const QString &node_id, const QString &title);
  Q_INVOKABLE void configureFeed(const QString &node_id,
                                 const QString &title,
                                 const QString &feed_url,
                                 const QString &comment,
                                 bool use_default_refresh_interval,
                                 int refresh_interval_hours,
                                 int archive_mode,
                                 int archive_limit);
  Q_INVOKABLE void deleteNode(const QString &node_id);
  Q_INVOKABLE void refreshFeed(const QString &node_id);
  Q_INVOKABLE void moveNode(const QString &node_id, const QString &parent_id);
  Q_INVOKABLE void selectNode(const QString &node_id);
  Q_INVOKABLE void selectArticleRow(int row);
  Q_INVOKABLE void markAllArticlesRead(const QString &node_id);
  Q_INVOKABLE void updateUserRefreshIntervalHours(int hours);
  Q_INVOKABLE void updateUserSettings(int refresh_hours, int archive_mode, int archive_limit);
  Q_INVOKABLE void updateUserArchiveSettings(int mode, int limit);
  Q_INVOKABLE void markSelectedArticleUnread();
  Q_INVOKABLE void toggleSelectedArticleQueued();
  Q_INVOKABLE void openSelectedArticle(const QString &profile_id = QString{});
  Q_INVOKABLE void openArticleAtRow(int row, const QString &profile_id = QString{});
  Q_INVOKABLE void clearStatusBar();
  Q_INVOKABLE void requestFeedTitleLookup(const QString &feed_url);
  Q_INVOKABLE void loadMoreArticles();
  Q_INVOKABLE void setFeedFilterText(const QString &text);
  Q_INVOKABLE void searchArticles(const QString &query);
  Q_INVOKABLE void setShowingReadArticles(bool value);
  Q_INVOKABLE void toggleShowingReadArticles();
  Q_INVOKABLE void copyArticleUrl(int row);
  Q_INVOKABLE void importFeeds();
  Q_INVOKABLE void exportFeeds();

 public slots:
  void onPeerMaterialStored();

 signals:
  void connectionStatusChanged();
  void feedTitleLookupFinished(const QString &feed_url, const QString &title);
  void statusBarChanged();
  void selectedNodeIdChanged();
  void userSettingsChanged();
  void unreadCountChanged();
  void previewChanged();
  void selectedArticleStateChanged();
  void articlePagingChanged();
  void articleSearchChanged();
  void articleFilterChanged();
  void serverInfoChanged();

 private:
  struct CachedArticlePage {
    QVector<ArticleData> articles;
    bool has_more = false;
    int next_offset = 0;
  };

  void setConnectionStatus(const QString &value);
  void setStatusBarMessage(const UiStatusMessage &message);
  void runAsync(std::function<void()> work);
  void reloadArticlesForCurrentNode();
  void loadArticlesPage(const QString &node_id, bool append);
  void refreshUnreadCount();
  void scheduleReconnect(int delay_ms = 0);
  void performHealthCheck();
  [[nodiscard]] bool isNetworkReachable() const;
  void handleNetworkReachabilityChanged();
  void handleNetworkEnvironmentChanged();
  [[nodiscard]] QVector<ArticleData> enrichArticles(const QVector<ArticleData> &articles) const;
  [[nodiscard]] bool markSelectedArticleRead();
  void setPreviewFromArticle(const QVariantMap &article);
  void stopForNetworkLoss();
  void deferReconnectUntilNetworkChanges(const QString &detail);
  [[nodiscard]] QString currentNetworkEnvironmentFingerprint() const;
  [[nodiscard]] bool errorSuggestsServerRouteUnavailable(const QString &message) const;

  FeedTreeModel feed_tree_model_;
  ArticleListModel article_list_model_;
  BrowserProfileManager browser_profile_manager_;
  SecretStore secret_store_;
  AppClient app_client_;
  QString connection_status_ = tr("Disconnected");
  UiStatusMessage status_bar_message_;
  QString selected_node_id_ = QStringLiteral("__root__");
  int default_refresh_interval_hours_ = 12;
  int default_archive_mode_ = 1;
  int default_archive_limit_ = 1;
  int unread_count_ = 0;
  QString preview_title_;
  QString preview_meta_;
  QString preview_content_;
  QString selected_article_link_;
  QString server_version_;
  QString server_database_name_;
  QString server_database_version_;
  QHash<QString, CachedArticlePage> article_cache_;
  QTimer health_check_timer_;
  QTimer network_change_timer_;
  QTimer reconnect_timer_;
  QString loaded_articles_node_id_ = QStringLiteral("__root__");
  QString article_search_query_;
  QString last_network_environment_fingerprint_;
  bool showing_read_articles_ = true;
  int next_article_offset_ = 0;
  bool can_load_more_articles_ = false;
  bool loading_more_articles_ = false;
  bool reconnect_in_progress_ = false;
  bool waiting_for_network_change_ = false;
};

}  // namespace onerss::desktop
