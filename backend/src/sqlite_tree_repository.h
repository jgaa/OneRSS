#pragma once

#include "sqlite.h"
#include "tree_repository.h"

#include <mutex>

namespace onerss::backend {

class SqliteTreeRepository final : public TreeRepository {
 public:
  explicit SqliteTreeRepository(const std::filesystem::path &database_path);

  [[nodiscard]] std::vector<TreeNodeRecord> listNodesForUser(const std::string &user_id) override;
  [[nodiscard]] TreeNodeRecord createFolder(const std::string &user_id,
                                            const std::string &parent_id,
                                            const std::string &title) override;
  [[nodiscard]] TreeNodeRecord createFeed(const std::string &user_id,
                                          const std::string &parent_id,
                                          const std::string &title,
                                          const std::string &feed_url,
                                          const std::string &comment) override;
  [[nodiscard]] TreeNodeRecord updateNode(const std::string &user_id,
                                          const TreeNodeRecord &node) override;
  void deleteNode(const std::string &user_id, const std::string &node_id) override;
  [[nodiscard]] TreeNodeRecord getNode(const std::string &user_id, const std::string &node_id) override;
  [[nodiscard]] std::vector<TreeNodeRecord> listAllFeeds() override;
  [[nodiscard]] std::size_t upsertArticles(const std::string &user_id,
                                           const std::string &node_id,
                                           const std::vector<ArticleRecord> &articles) override;
  void applyArchivePolicy(const std::string &user_id,
                          const std::string &node_id,
                          onerss::pb::ArchiveMode archive_mode,
                          std::uint32_t archive_limit,
                          const std::vector<std::string> &current_guids) override;
  [[nodiscard]] std::size_t unreadCount(const std::string &user_id) override;
  [[nodiscard]] std::size_t markArticleRead(const std::string &user_id,
                                            const std::string &node_id,
                                            const std::string &article_id) override;
  [[nodiscard]] std::size_t markArticleUnread(const std::string &user_id,
                                              const std::string &node_id,
                                              const std::string &article_id) override;
  [[nodiscard]] std::size_t queueArticle(const std::string &user_id,
                                         const std::string &node_id,
                                         const std::string &article_id) override;
  [[nodiscard]] std::size_t unqueueArticle(const std::string &user_id,
                                           const std::string &node_id,
                                           const std::string &article_id) override;
  [[nodiscard]] std::size_t markAllArticlesRead(const std::string &user_id,
                                                const std::string &node_id) override;
  [[nodiscard]] std::vector<ArticleRecord> listArticles(const std::string &user_id,
                                                        const std::string &node_id,
                                                        const std::string &title_query,
                                                        bool unread_only,
                                                        std::size_t offset,
                                                        std::size_t limit) override;

 private:
  void ensureSchema();
  void migrateDataIfNeeded();
  void scanAndSanitizeArticles();
  void scanAndSanitizeQueuedArticles();
  void validateReparentLocked(const std::string &user_id,
                              const std::string &node_id,
                              const std::string &parent_id);
  void validateParentLocked(const std::string &user_id,
                            const std::string &parent_id,
                            onerss::pb::TreeNode::Type parent_type_expected);
  [[nodiscard]] bool isDescendantLocked(const std::string &user_id,
                                        const std::string &ancestor_id,
                                        const std::string &node_id);
  [[nodiscard]] TreeNodeRecord fetchNodeLocked(const std::string &user_id, const std::string &node_id);
  [[nodiscard]] static std::string newUuid();
  static void bindText(sqlite3_stmt &statement, int index, const std::string &value);

  sqlite_ptr db_;
  std::mutex mutex_;
};

}  // namespace onerss::backend
