#pragma once

#include "article_list_model.h"
#include "app_client.h"
#include "feed_tree_model.h"
#include "secret_store.h"
#include "user_notification.h"

#include <QObject>

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
  Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
  Q_PROPERTY(QString previewTitle READ previewTitle NOTIFY previewChanged)
  Q_PROPERTY(QString previewMeta READ previewMeta NOTIFY previewChanged)
  Q_PROPERTY(QString previewContent READ previewContent NOTIFY previewChanged)
  Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
  Q_PROPERTY(bool canLoadMoreArticles READ canLoadMoreArticles NOTIFY articlePagingChanged)
  Q_PROPERTY(bool loadingMoreArticles READ loadingMoreArticles NOTIFY articlePagingChanged)

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
  [[nodiscard]] int unreadCount() const;
  [[nodiscard]] QString previewTitle() const;
  [[nodiscard]] QString previewMeta() const;
  [[nodiscard]] QString previewContent() const;
  [[nodiscard]] bool hasPreview() const;
  [[nodiscard]] bool canLoadMoreArticles() const;
  [[nodiscard]] bool loadingMoreArticles() const;

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
                                 int refresh_interval_hours);
  Q_INVOKABLE void deleteNode(const QString &node_id);
  Q_INVOKABLE void refreshFeed(const QString &node_id);
  Q_INVOKABLE void moveNode(const QString &node_id, const QString &parent_id);
  Q_INVOKABLE void selectNode(const QString &node_id);
  Q_INVOKABLE void selectArticleRow(int row);
  Q_INVOKABLE void markAllArticlesRead(const QString &node_id);
  Q_INVOKABLE void updateUserRefreshIntervalHours(int hours);
  Q_INVOKABLE void openSelectedArticle();
  Q_INVOKABLE void clearStatusBar();
  Q_INVOKABLE void requestFeedTitleLookup(const QString &feed_url);
  Q_INVOKABLE void loadMoreArticles();

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
  void articlePagingChanged();

 private:
  void setConnectionStatus(const QString &value);
  void setStatusBarMessage(const UiStatusMessage &message);
  void runAsync(std::function<void()> work);
  void reloadArticlesForCurrentNode();
  void loadArticlesPage(const QString &node_id, bool append);
  void refreshUnreadCount();
  void setPreviewFromArticle(const QVariantMap &article);

  FeedTreeModel feed_tree_model_;
  ArticleListModel article_list_model_;
  SecretStore secret_store_;
  AppClient app_client_;
  QString connection_status_ = tr("Disconnected");
  UiStatusMessage status_bar_message_;
  QString selected_node_id_ = QStringLiteral("__root__");
  int default_refresh_interval_hours_ = 12;
  int unread_count_ = 0;
  QString preview_title_;
  QString preview_meta_;
  QString preview_content_;
  QString selected_article_link_;
  QString loaded_articles_node_id_ = QStringLiteral("__root__");
  int next_article_offset_ = 0;
  bool can_load_more_articles_ = false;
  bool loading_more_articles_ = false;
};

}  // namespace onerss::desktop
