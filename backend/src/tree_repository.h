#pragma once

#include "article_types.h"
#include "onerss.pb.h"

#include <cstdint>
#include <memory>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace onerss::backend {

struct TreeNodeRecord {
  std::string node_id;
  std::string user_id;
  std::string parent_id;
  onerss::pb::TreeNode::Type type = onerss::pb::TreeNode::TYPE_FOLDER;
  std::string title;
  std::string feed_url;
  std::string comment;
  bool use_default_refresh_interval = true;
  std::uint32_t refresh_interval_hours = 12;
};

class TreeRepository {
 public:
  using ptr_t = std::unique_ptr<TreeRepository>;

  virtual ~TreeRepository() = default;

  [[nodiscard]] virtual std::vector<TreeNodeRecord> listNodesForUser(const std::string &user_id) = 0;
  [[nodiscard]] virtual TreeNodeRecord createFolder(const std::string &user_id,
                                                    const std::string &parent_id,
                                                    const std::string &title) = 0;
  [[nodiscard]] virtual TreeNodeRecord createFeed(const std::string &user_id,
                                                  const std::string &parent_id,
                                                  const std::string &title,
                                                  const std::string &feed_url,
                                                  const std::string &comment) = 0;
  [[nodiscard]] virtual TreeNodeRecord updateNode(const std::string &user_id,
                                                  const TreeNodeRecord &node) = 0;
  virtual void deleteNode(const std::string &user_id, const std::string &node_id) = 0;
  [[nodiscard]] virtual TreeNodeRecord getNode(const std::string &user_id, const std::string &node_id) = 0;
  [[nodiscard]] virtual std::vector<TreeNodeRecord> listAllFeeds() = 0;
  [[nodiscard]] virtual std::size_t upsertArticles(const std::string &user_id,
                                                   const std::string &node_id,
                                                   const std::vector<ArticleRecord> &articles) = 0;
  [[nodiscard]] virtual std::size_t unreadCount(const std::string &user_id) = 0;
  [[nodiscard]] virtual std::size_t markArticleRead(const std::string &user_id,
                                                    const std::string &node_id,
                                                    const std::string &article_id) = 0;
  [[nodiscard]] virtual std::size_t markArticleUnread(const std::string &user_id,
                                                      const std::string &node_id,
                                                      const std::string &article_id) = 0;
  [[nodiscard]] virtual std::size_t markAllArticlesRead(const std::string &user_id,
                                                        const std::string &node_id) = 0;
  [[nodiscard]] virtual std::vector<ArticleRecord> listArticles(const std::string &user_id,
                                                                const std::string &node_id,
                                                                const std::string &title_query,
                                                                bool unread_only,
                                                                std::size_t offset,
                                                                std::size_t limit) = 0;
};

onerss::pb::TreeNode toProto(const TreeNodeRecord &record);
TreeNodeRecord fromProto(const std::string &user_id, const onerss::pb::TreeNode &node);

}  // namespace onerss::backend
