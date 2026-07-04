#include "sqlite_tree_repository.h"

#include "logging.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sqlite3.h>

#include <array>
#include <memory>
#include <stdexcept>

namespace onerss::backend {
namespace {

struct StatementDeleter {
  void operator()(sqlite3_stmt *statement) const noexcept {
    if (statement != nullptr) {
      sqlite3_finalize(statement);
    }
  }
};

using statement_ptr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

statement_ptr prepare(sqlite3 &db, const char *sql) {
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(&db, sql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error{sqlite3_errmsg(&db)};
  }
  return statement_ptr{statement};
}

void checkStepDone(sqlite3 &db, const int rc) {
  if (rc != SQLITE_DONE) {
    throw std::runtime_error{sqlite3_errmsg(&db)};
  }
}

std::string columnText(sqlite3_stmt &statement, const int index) {
  const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(&statement, index));
  return data != nullptr ? std::string{data} : std::string{};
}

int columnInt(sqlite3_stmt &statement, const int index) {
  return sqlite3_column_int(&statement, index);
}

}  // namespace

SqliteTreeRepository::SqliteTreeRepository(const std::filesystem::path &database_path)
    : db_{openDatabase(database_path)} {
  ensureSchema();
}

std::vector<TreeNodeRecord> SqliteTreeRepository::listNodesForUser(const std::string &user_id) {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_,
                        "SELECT id, user_id, parent_id, node_type, title, feed_url, comment, "
                        "use_default_refresh_interval, refresh_interval_hours "
                        "FROM tree_nodes WHERE user_id = ?1 ORDER BY node_type, title, id");
  bindText(*select, 1, user_id);

  std::vector<TreeNodeRecord> nodes;
  while (true) {
    const auto rc = sqlite3_step(select.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }

    nodes.push_back(TreeNodeRecord{
      .node_id = columnText(*select, 0),
      .user_id = columnText(*select, 1),
      .parent_id = columnText(*select, 2),
      .type = static_cast<onerss::pb::TreeNode::Type>(columnInt(*select, 3)),
      .title = columnText(*select, 4),
      .feed_url = columnText(*select, 5),
      .comment = columnText(*select, 6),
      .use_default_refresh_interval = columnInt(*select, 7) != 0,
      .refresh_interval_hours = static_cast<std::uint32_t>(std::max(1, columnInt(*select, 8))),
    });
  }

  LOG_TRACE << "Listed " << nodes.size() << " tree nodes for user=" << user_id;
  return nodes;
}

TreeNodeRecord SqliteTreeRepository::createFolder(const std::string &user_id,
                                                  const std::string &parent_id,
                                                  const std::string &title) {
  std::scoped_lock lock{mutex_};
  if (!parent_id.empty()) {
    validateParentLocked(user_id, parent_id, onerss::pb::TreeNode::TYPE_FOLDER);
  }

  const auto node_id = newUuid();
  auto insert = prepare(
    *db_,
    "INSERT INTO tree_nodes(id, user_id, parent_id, node_type, title, feed_url, comment, "
    "use_default_refresh_interval, refresh_interval_hours) "
    "VALUES(?1, ?2, ?3, ?4, ?5, '', '', 1, 12)");
  bindText(*insert, 1, node_id);
  bindText(*insert, 2, user_id);
  bindText(*insert, 3, parent_id);
  sqlite3_bind_int(insert.get(), 4, onerss::pb::TreeNode::TYPE_FOLDER);
  bindText(*insert, 5, title);
  checkStepDone(*db_, sqlite3_step(insert.get()));
  return fetchNodeLocked(user_id, node_id);
}

TreeNodeRecord SqliteTreeRepository::createFeed(const std::string &user_id,
                                                const std::string &parent_id,
                                                const std::string &title,
                                                const std::string &feed_url,
                                                const std::string &comment) {
  std::scoped_lock lock{mutex_};
  if (!parent_id.empty()) {
    validateParentLocked(user_id, parent_id, onerss::pb::TreeNode::TYPE_FOLDER);
  }

  const auto node_id = newUuid();
  auto insert = prepare(
    *db_,
    "INSERT INTO tree_nodes(id, user_id, parent_id, node_type, title, feed_url, comment, "
    "use_default_refresh_interval, refresh_interval_hours) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, 1, 12)");
  bindText(*insert, 1, node_id);
  bindText(*insert, 2, user_id);
  bindText(*insert, 3, parent_id);
  sqlite3_bind_int(insert.get(), 4, onerss::pb::TreeNode::TYPE_FEED);
  bindText(*insert, 5, title);
  bindText(*insert, 6, feed_url);
  bindText(*insert, 7, comment);
  checkStepDone(*db_, sqlite3_step(insert.get()));
  return fetchNodeLocked(user_id, node_id);
}

TreeNodeRecord SqliteTreeRepository::updateNode(const std::string &user_id, const TreeNodeRecord &node) {
  std::scoped_lock lock{mutex_};
  validateReparentLocked(user_id, node.node_id, node.parent_id);

  auto update = prepare(
    *db_,
    "UPDATE tree_nodes SET parent_id = ?1, title = ?2, feed_url = ?3, comment = ?4, "
    "use_default_refresh_interval = ?5, refresh_interval_hours = ?6 "
    "WHERE id = ?7 AND user_id = ?8 AND node_type = ?9");
  bindText(*update, 1, node.parent_id);
  bindText(*update, 2, node.title);
  bindText(*update, 3, node.feed_url);
  bindText(*update, 4, node.comment);
  sqlite3_bind_int(update.get(), 5, node.use_default_refresh_interval ? 1 : 0);
  sqlite3_bind_int(update.get(), 6, static_cast<int>(std::max<std::uint32_t>(1, node.refresh_interval_hours)));
  bindText(*update, 7, node.node_id);
  bindText(*update, 8, user_id);
  sqlite3_bind_int(update.get(), 9, node.type);
  checkStepDone(*db_, sqlite3_step(update.get()));

  if (sqlite3_changes(db_.get()) == 0) {
    throw std::runtime_error{"tree node not found"};
  }

  return fetchNodeLocked(user_id, node.node_id);
}

void SqliteTreeRepository::deleteNode(const std::string &user_id, const std::string &node_id) {
  std::scoped_lock lock{mutex_};
  auto remove = prepare(*db_, "DELETE FROM tree_nodes WHERE id = ?1 AND user_id = ?2");
  bindText(*remove, 1, node_id);
  bindText(*remove, 2, user_id);
  checkStepDone(*db_, sqlite3_step(remove.get()));

  if (sqlite3_changes(db_.get()) == 0) {
    throw std::runtime_error{"tree node not found"};
  }
}

TreeNodeRecord SqliteTreeRepository::getNode(const std::string &user_id, const std::string &node_id) {
  std::scoped_lock lock{mutex_};
  return fetchNodeLocked(user_id, node_id);
}

std::vector<TreeNodeRecord> SqliteTreeRepository::listAllFeeds() {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_,
                        "SELECT id, user_id, parent_id, node_type, title, feed_url, comment, "
                        "use_default_refresh_interval, refresh_interval_hours "
                        "FROM tree_nodes WHERE node_type = ?1 ORDER BY user_id, id");
  sqlite3_bind_int(select.get(), 1, onerss::pb::TreeNode::TYPE_FEED);

  std::vector<TreeNodeRecord> nodes;
  while (true) {
    const auto rc = sqlite3_step(select.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }
    nodes.push_back(TreeNodeRecord{
      .node_id = columnText(*select, 0),
      .user_id = columnText(*select, 1),
      .parent_id = columnText(*select, 2),
      .type = static_cast<onerss::pb::TreeNode::Type>(columnInt(*select, 3)),
      .title = columnText(*select, 4),
      .feed_url = columnText(*select, 5),
      .comment = columnText(*select, 6),
      .use_default_refresh_interval = columnInt(*select, 7) != 0,
      .refresh_interval_hours = static_cast<std::uint32_t>(std::max(1, columnInt(*select, 8))),
    });
  }
  return nodes;
}

std::size_t SqliteTreeRepository::upsertArticles(const std::string &user_id,
                                                 const std::string &node_id,
                                                 const std::vector<ArticleRecord> &articles) {
  std::scoped_lock lock{mutex_};
  validateParentLocked(user_id, node_id, onerss::pb::TreeNode::TYPE_FEED);

  auto exists = prepare(*db_,
                        "SELECT 1 FROM articles WHERE user_id = ?1 AND node_id = ?2 AND article_guid = ?3 LIMIT 1");
  auto insert = prepare(
    *db_,
    "INSERT INTO articles(id, user_id, node_id, article_guid, title, link_url, published_at, author, content, is_read) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 0) "
    "ON CONFLICT(user_id, node_id, article_guid) DO UPDATE SET "
    "title = excluded.title, "
    "link_url = excluded.link_url, "
    "published_at = excluded.published_at, "
    "author = excluded.author, "
    "content = excluded.content");

  std::size_t new_entries = 0;
  for (const auto &article : articles) {
    sqlite3_reset(exists.get());
    sqlite3_clear_bindings(exists.get());
    bindText(*exists, 1, user_id);
    bindText(*exists, 2, node_id);
    bindText(*exists, 3, article.guid);
    const auto exists_rc = sqlite3_step(exists.get());
    if (exists_rc == SQLITE_ROW) {
      // already present
    } else if (exists_rc == SQLITE_DONE) {
      ++new_entries;
    } else {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }

    const auto article_id = article.article_id.empty() ? newUuid() : article.article_id;
    sqlite3_reset(insert.get());
    sqlite3_clear_bindings(insert.get());
    bindText(*insert, 1, article_id);
    bindText(*insert, 2, user_id);
    bindText(*insert, 3, node_id);
    bindText(*insert, 4, article.guid);
    bindText(*insert, 5, article.title);
    bindText(*insert, 6, article.link_url);
    bindText(*insert, 7, article.published_at);
    bindText(*insert, 8, article.author);
    bindText(*insert, 9, article.content);
    checkStepDone(*db_, sqlite3_step(insert.get()));
  }
  return new_entries;
}

std::vector<ArticleRecord> SqliteTreeRepository::listArticles(const std::string &user_id,
                                                              const std::string &node_id,
                                                              const std::size_t offset,
                                                              const std::size_t limit) {
  std::scoped_lock lock{mutex_};
  if (limit == 0) {
    return {};
  }
  const bool all_feeds = node_id.empty();
  auto select = prepare(
    *db_,
    all_feeds
      ? "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, is_read "
        "FROM articles WHERE user_id = ?1 ORDER BY published_at DESC, id DESC LIMIT ?2 OFFSET ?3"
      : "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, is_read "
        "FROM articles WHERE user_id = ?1 AND node_id = ?2 ORDER BY published_at DESC, id DESC LIMIT ?3 OFFSET ?4");
  bindText(*select, 1, user_id);
  if (!all_feeds) {
    bindText(*select, 2, node_id);
    sqlite3_bind_int64(select.get(), 3, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(select.get(), 4, static_cast<sqlite3_int64>(offset));
  } else {
    sqlite3_bind_int64(select.get(), 2, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(select.get(), 3, static_cast<sqlite3_int64>(offset));
  }

  std::vector<ArticleRecord> articles;
  while (true) {
    const auto rc = sqlite3_step(select.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }

    articles.push_back(ArticleRecord{
      .article_id = columnText(*select, 0),
      .user_id = columnText(*select, 1),
      .node_id = columnText(*select, 2),
      .guid = columnText(*select, 3),
      .title = columnText(*select, 4),
      .link_url = columnText(*select, 5),
      .published_at = columnText(*select, 6),
      .author = columnText(*select, 7),
      .content = columnText(*select, 8),
      .is_read = columnInt(*select, 9) != 0,
    });
  }

  return articles;
}

std::size_t SqliteTreeRepository::unreadCount(const std::string &user_id) {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_, "SELECT COUNT(*) FROM articles WHERE user_id = ?1 AND is_read = 0");
  bindText(*select, 1, user_id);
  const auto rc = sqlite3_step(select.get());
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }
  return static_cast<std::size_t>(sqlite3_column_int64(select.get(), 0));
}

std::size_t SqliteTreeRepository::markArticleRead(const std::string &user_id,
                                                  const std::string &node_id,
                                                  const std::string &article_id) {
  std::scoped_lock lock{mutex_};
  auto update = prepare(*db_,
                        "UPDATE articles SET is_read = 1 "
                        "WHERE user_id = ?1 AND node_id = ?2 AND id = ?3 AND is_read = 0");
  bindText(*update, 1, user_id);
  bindText(*update, 2, node_id);
  bindText(*update, 3, article_id);
  checkStepDone(*db_, sqlite3_step(update.get()));
  return static_cast<std::size_t>(sqlite3_changes(db_.get()));
}

std::size_t SqliteTreeRepository::markAllArticlesRead(const std::string &user_id, const std::string &node_id) {
  std::scoped_lock lock{mutex_};
  validateParentLocked(user_id, node_id, onerss::pb::TreeNode::TYPE_FEED);
  auto update = prepare(*db_,
                        "UPDATE articles SET is_read = 1 "
                        "WHERE user_id = ?1 AND node_id = ?2 AND is_read = 0");
  bindText(*update, 1, user_id);
  bindText(*update, 2, node_id);
  checkStepDone(*db_, sqlite3_step(update.get()));
  return static_cast<std::size_t>(sqlite3_changes(db_.get()));
}

void SqliteTreeRepository::ensureSchema() {
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS tree_nodes ("
          "  id TEXT PRIMARY KEY,"
          "  user_id TEXT NOT NULL,"
          "  parent_id TEXT NOT NULL DEFAULT '',"
          "  node_type INTEGER NOT NULL,"
          "  title TEXT NOT NULL,"
          "  feed_url TEXT NOT NULL DEFAULT '',"
          "  comment TEXT NOT NULL DEFAULT '',"
          "  use_default_refresh_interval INTEGER NOT NULL DEFAULT 1,"
          "  refresh_interval_hours INTEGER NOT NULL DEFAULT 12,"
          "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
          ");");
  try {
    execute(*db_, "ALTER TABLE tree_nodes ADD COLUMN use_default_refresh_interval INTEGER NOT NULL DEFAULT 1;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  try {
    execute(*db_, "ALTER TABLE tree_nodes ADD COLUMN refresh_interval_hours INTEGER NOT NULL DEFAULT 12;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  execute(*db_, "CREATE INDEX IF NOT EXISTS tree_nodes_user_idx ON tree_nodes(user_id);");
  execute(*db_, "CREATE INDEX IF NOT EXISTS tree_nodes_parent_idx ON tree_nodes(user_id, parent_id);");
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS articles ("
          "  id TEXT PRIMARY KEY,"
          "  user_id TEXT NOT NULL,"
          "  node_id TEXT NOT NULL,"
          "  article_guid TEXT NOT NULL,"
          "  title TEXT NOT NULL,"
          "  link_url TEXT NOT NULL DEFAULT '',"
          "  published_at TEXT NOT NULL DEFAULT '',"
          "  author TEXT NOT NULL DEFAULT '',"
          "  content TEXT NOT NULL DEFAULT '',"
          "  is_read INTEGER NOT NULL DEFAULT 0,"
          "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
          "  FOREIGN KEY(node_id) REFERENCES tree_nodes(id) ON DELETE CASCADE,"
          "  UNIQUE(user_id, node_id, article_guid)"
          ");");
  try {
    execute(*db_, "ALTER TABLE articles ADD COLUMN is_read INTEGER NOT NULL DEFAULT 0;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  execute(*db_, "CREATE INDEX IF NOT EXISTS articles_user_node_idx ON articles(user_id, node_id);");
}

void SqliteTreeRepository::validateReparentLocked(const std::string &user_id,
                                                  const std::string &node_id,
                                                  const std::string &parent_id) {
  if (node_id.empty()) {
    throw std::runtime_error{"node_id is required"};
  }
  if (node_id == parent_id) {
    throw std::runtime_error{"node cannot be its own parent"};
  }

  static_cast<void>(fetchNodeLocked(user_id, node_id));
  if (parent_id.empty()) {
    return;
  }

  validateParentLocked(user_id, parent_id, onerss::pb::TreeNode::TYPE_FOLDER);
  if (isDescendantLocked(user_id, node_id, parent_id)) {
    throw std::runtime_error{"node cannot be moved into its own descendant"};
  }
}

void SqliteTreeRepository::validateParentLocked(const std::string &user_id,
                                                const std::string &parent_id,
                                                const onerss::pb::TreeNode::Type parent_type_expected) {
  const auto parent = fetchNodeLocked(user_id, parent_id);
  if (parent.type != parent_type_expected) {
    throw std::runtime_error{"parent node type is not allowed"};
  }
}

bool SqliteTreeRepository::isDescendantLocked(const std::string &user_id,
                                              const std::string &ancestor_id,
                                              const std::string &node_id) {
  auto current = fetchNodeLocked(user_id, node_id);
  while (true) {
    if (current.node_id == ancestor_id) {
      return true;
    }
    if (current.parent_id.empty()) {
      return false;
    }
    current = fetchNodeLocked(user_id, current.parent_id);
  }
}

TreeNodeRecord SqliteTreeRepository::fetchNodeLocked(const std::string &user_id, const std::string &node_id) {
  auto select = prepare(*db_,
                        "SELECT id, user_id, parent_id, node_type, title, feed_url, comment, "
                        "use_default_refresh_interval, refresh_interval_hours "
                        "FROM tree_nodes WHERE id = ?1 AND user_id = ?2");
  bindText(*select, 1, node_id);
  bindText(*select, 2, user_id);
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    throw std::runtime_error{"tree node not found"};
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }

  return TreeNodeRecord{
    .node_id = columnText(*select, 0),
    .user_id = columnText(*select, 1),
    .parent_id = columnText(*select, 2),
    .type = static_cast<onerss::pb::TreeNode::Type>(columnInt(*select, 3)),
    .title = columnText(*select, 4),
    .feed_url = columnText(*select, 5),
    .comment = columnText(*select, 6),
    .use_default_refresh_interval = columnInt(*select, 7) != 0,
    .refresh_interval_hours = static_cast<std::uint32_t>(std::max(1, columnInt(*select, 8))),
  };
}

std::string SqliteTreeRepository::newUuid() {
  return boost::uuids::to_string(boost::uuids::random_generator()());
}

void SqliteTreeRepository::bindText(sqlite3_stmt &statement, const int index, const std::string &value) {
  if (sqlite3_bind_text(&statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT)
      != SQLITE_OK) {
    throw std::runtime_error{"sqlite3_bind_text failed"};
  }
}

}  // namespace onerss::backend
