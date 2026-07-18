#include "article_sanitizer.h"
#include "sqlite_tree_repository.h"

#include "logging.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace onerss::backend {
namespace {

constexpr std::int64_t kCurrentDataVersion = 3;
constexpr std::string_view kQueueNodeId = "__queue__";

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

std::int64_t columnInt64(sqlite3_stmt &statement, const int index) {
  return sqlite3_column_int64(&statement, index);
}

std::int64_t readDataVersion(sqlite3 &db) {
  auto select = prepare(db, "SELECT value FROM system_kv WHERE key = 'data_version'");
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    return 0;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(&db)};
  }
  const auto value = columnText(*select, 0);
  if (value.empty()) {
    return 0;
  }
  return std::stoll(value);
}

void writeDataVersion(sqlite3 &db, const std::int64_t version) {
  auto update = prepare(db, "INSERT OR REPLACE INTO system_kv(key, value) VALUES('data_version', ?1)");
  const auto version_text = std::to_string(version);
  if (sqlite3_bind_text(update.get(), 1, version_text.c_str(), static_cast<int>(version_text.size()), SQLITE_TRANSIENT)
      != SQLITE_OK) {
    throw std::runtime_error{sqlite3_errmsg(&db)};
  }
  checkStepDone(db, sqlite3_step(update.get()));
}

}  // namespace

SqliteTreeRepository::SqliteTreeRepository(const std::filesystem::path &database_path)
    : db_{openDatabase(database_path)} {
  ensureSchema();
  migrateDataIfNeeded();
}

std::vector<TreeNodeRecord> SqliteTreeRepository::listNodesForUser(const std::string &user_id) {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_,
                        "SELECT id, user_id, parent_id, node_type, title, feed_url, comment, "
                        "use_default_refresh_interval, refresh_interval_hours, archive_mode, archive_limit "
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
      .archive_mode = static_cast<onerss::pb::ArchiveMode>(std::clamp(columnInt(*select, 9),
                                                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_USE_DEFAULT),
                                                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED))),
      .archive_limit = static_cast<std::uint32_t>(std::max(0, columnInt(*select, 10))),
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
    "use_default_refresh_interval, refresh_interval_hours, archive_mode, archive_limit) "
    "VALUES(?1, ?2, ?3, ?4, ?5, '', '', 1, 12, 0, 0)");
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
    "use_default_refresh_interval, refresh_interval_hours, archive_mode, archive_limit) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, 1, 12, 0, 0)");
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
    "use_default_refresh_interval = ?5, refresh_interval_hours = ?6, archive_mode = ?7, archive_limit = ?8 "
    "WHERE id = ?9 AND user_id = ?10 AND node_type = ?11");
  bindText(*update, 1, node.parent_id);
  bindText(*update, 2, node.title);
  bindText(*update, 3, node.feed_url);
  bindText(*update, 4, node.comment);
  sqlite3_bind_int(update.get(), 5, node.use_default_refresh_interval ? 1 : 0);
  sqlite3_bind_int(update.get(), 6, static_cast<int>(std::max<std::uint32_t>(1, node.refresh_interval_hours)));
  sqlite3_bind_int(update.get(), 7, node.archive_mode);
  sqlite3_bind_int(update.get(), 8, static_cast<int>(node.archive_limit));
  bindText(*update, 9, node.node_id);
  bindText(*update, 10, user_id);
  sqlite3_bind_int(update.get(), 11, node.type);
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
                        "use_default_refresh_interval, refresh_interval_hours, archive_mode, archive_limit "
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
      .archive_mode = static_cast<onerss::pb::ArchiveMode>(std::clamp(columnInt(*select, 9),
                                                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_USE_DEFAULT),
                                                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED))),
      .archive_limit = static_cast<std::uint32_t>(std::max(0, columnInt(*select, 10))),
    });
  }
  return nodes;
}

namespace {

ArticleRecord readArticleRecord(sqlite3_stmt &statement) {
  return ArticleRecord{
    .article_id = columnText(statement, 0),
    .user_id = columnText(statement, 1),
    .node_id = columnText(statement, 2),
    .guid = columnText(statement, 3),
    .title = columnText(statement, 4),
    .link_url = columnText(statement, 5),
    .published_at = columnText(statement, 6),
    .author = columnText(statement, 7),
    .content = columnText(statement, 8),
    .is_read = columnInt(statement, 9) != 0,
    .is_queued = columnInt(statement, 10) != 0,
    .original_article_id = columnText(statement, 11),
  };
}

}  // namespace

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
    auto sanitized = sanitizeArticleForStorage(article);
    if (!sanitized.accepted) {
      LOG_WARN << "Rejected article for node=" << node_id
               << " title=" << article.title
               << " guid=" << article.guid
               << " reason=" << sanitized.rejection_reason;
      continue;
    }
    if (sanitized.content_control_bytes_normalized) {
      LOG_WARN << "Normalized control bytes in article content for node=" << node_id
               << " title=" << sanitized.article.title
               << " guid=" << sanitized.article.guid;
    }
    if (sanitized.content_capped) {
      LOG_WARN << "Capped article content for node=" << node_id
               << " title=" << sanitized.article.title
               << " guid=" << sanitized.article.guid
               << " max_bytes=" << maxStoredArticleContentBytes();
    }

    sqlite3_reset(exists.get());
    sqlite3_clear_bindings(exists.get());
    bindText(*exists, 1, user_id);
    bindText(*exists, 2, node_id);
    bindText(*exists, 3, sanitized.article.guid);
    const auto exists_rc = sqlite3_step(exists.get());
    if (exists_rc == SQLITE_ROW) {
      // already present
    } else if (exists_rc == SQLITE_DONE) {
      ++new_entries;
    } else {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }

    const auto article_id = sanitized.article.article_id.empty() ? newUuid() : sanitized.article.article_id;
    sqlite3_reset(insert.get());
    sqlite3_clear_bindings(insert.get());
    bindText(*insert, 1, article_id);
    bindText(*insert, 2, user_id);
    bindText(*insert, 3, node_id);
    bindText(*insert, 4, sanitized.article.guid);
    bindText(*insert, 5, sanitized.article.title);
    bindText(*insert, 6, sanitized.article.link_url);
    bindText(*insert, 7, sanitized.article.published_at);
    bindText(*insert, 8, sanitized.article.author);
    bindText(*insert, 9, sanitized.article.content);
    checkStepDone(*db_, sqlite3_step(insert.get()));
  }
  return new_entries;
}

void SqliteTreeRepository::applyArchivePolicy(const std::string &user_id,
                                              const std::string &node_id,
                                              const onerss::pb::ArchiveMode archive_mode,
                                              const std::uint32_t archive_limit,
                                              const std::vector<std::string> &current_guids) {
  std::scoped_lock lock{mutex_};
  validateParentLocked(user_id, node_id, onerss::pb::TreeNode::TYPE_FEED);
  if (archive_mode == onerss::pb::ARCHIVE_MODE_KEEP_ALL) {
    return;
  }

  std::string sql;
  if (archive_mode == onerss::pb::ARCHIVE_MODE_LIMIT_ARTICLES) {
    sql = "DELETE FROM articles WHERE user_id = ?1 AND node_id = ?2 "
          "AND NOT EXISTS (SELECT 1 FROM queued_articles q WHERE q.original_article_id = articles.id) "
          "AND id NOT IN (SELECT id FROM articles WHERE user_id = ?1 AND node_id = ?2 "
          "ORDER BY CASE WHEN published_at = '' THEN 1 ELSE 0 END, published_at DESC, id DESC LIMIT ?3)";
  } else if (archive_mode == onerss::pb::ARCHIVE_MODE_DELETE_OLDER_THAN) {
    sql = "DELETE FROM articles WHERE user_id = ?1 AND node_id = ?2 "
          "AND published_at <> '' AND datetime(published_at) < datetime('now', printf('-%d days', ?3)) "
          "AND NOT EXISTS (SELECT 1 FROM queued_articles q WHERE q.original_article_id = articles.id)";
  } else if (archive_mode == onerss::pb::ARCHIVE_MODE_DISABLED) {
    sql = "DELETE FROM articles WHERE user_id = ?1 AND node_id = ?2 "
          "AND NOT EXISTS (SELECT 1 FROM queued_articles q WHERE q.original_article_id = articles.id)";
    if (!current_guids.empty()) {
      sql += " AND article_guid NOT IN (";
      for (std::size_t i = 0; i < current_guids.size(); ++i) {
        if (i != 0) {
          sql += ",";
        }
        sql += "?" + std::to_string(i + 3);
      }
      sql += ")";
    }
  } else {
    return;
  }

  auto remove = prepare(*db_, sql.c_str());
  bindText(*remove, 1, user_id);
  bindText(*remove, 2, node_id);
  if (archive_mode != onerss::pb::ARCHIVE_MODE_DISABLED) {
    sqlite3_bind_int(remove.get(), 3, static_cast<int>(std::max(1u, archive_limit)));
  } else {
    for (std::size_t i = 0; i < current_guids.size(); ++i) {
      bindText(*remove, static_cast<int>(i + 3), current_guids[i]);
    }
  }
  checkStepDone(*db_, sqlite3_step(remove.get()));
}

std::vector<ArticleRecord> SqliteTreeRepository::listArticles(const std::string &user_id,
                                                              const std::string &node_id,
                                                              const std::string &title_query,
                                                              const bool unread_only,
                                                              const std::size_t offset,
                                                              const std::size_t limit) {
  std::scoped_lock lock{mutex_};
  if (limit == 0) {
    return {};
  }
  const bool all_feeds = node_id.empty();
  const bool queue_view = node_id == kQueueNodeId;
  statement_ptr select;
  if (queue_view) {
    select = prepare(
      *db_,
      "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, is_read, 1, "
      "original_article_id "
      "FROM queued_articles WHERE user_id = ?1 "
      "AND (?2 = '' OR instr(lower(title), lower(?2)) > 0) "
      "AND (?3 = 0 OR is_read = 0) "
      "ORDER BY published_at DESC, id DESC LIMIT ?4 OFFSET ?5");
    bindText(*select, 1, user_id);
    bindText(*select, 2, title_query);
    sqlite3_bind_int(select.get(), 3, unread_only ? 1 : 0);
    sqlite3_bind_int64(select.get(), 4, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(select.get(), 5, static_cast<sqlite3_int64>(offset));
  } else if (all_feeds) {
    select = prepare(*db_,
                     "SELECT a.id, a.user_id, a.node_id, a.article_guid, a.title, a.link_url, a.published_at, "
                     "a.author, a.content, a.is_read, "
                     "CASE WHEN q.original_article_id IS NULL THEN 0 ELSE 1 END AS is_queued, "
                     "COALESCE(q.original_article_id, '') "
                     "FROM articles a "
                     "LEFT JOIN queued_articles q ON q.user_id = a.user_id AND q.original_article_id = a.id "
                     "WHERE a.user_id = ?1 "
                     "AND (?2 = '' OR instr(lower(a.title), lower(?2)) > 0) "
                     "AND (?3 = 0 OR a.is_read = 0) "
                     "ORDER BY a.published_at DESC, a.id DESC LIMIT ?4 OFFSET ?5");
    bindText(*select, 1, user_id);
    bindText(*select, 2, title_query);
    sqlite3_bind_int(select.get(), 3, unread_only ? 1 : 0);
    sqlite3_bind_int64(select.get(), 4, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(select.get(), 5, static_cast<sqlite3_int64>(offset));
  } else {
    const auto node = fetchNodeLocked(user_id, node_id);
    if (node.type == onerss::pb::TreeNode::TYPE_FOLDER) {
      select = prepare(
        *db_,
        "WITH RECURSIVE subtree(id, node_type) AS ("
        "  SELECT id, node_type FROM tree_nodes WHERE user_id = ?1 AND id = ?2"
        "  UNION ALL "
        "  SELECT child.id, child.node_type FROM tree_nodes child "
        "  JOIN subtree parent ON child.parent_id = parent.id "
        "  WHERE child.user_id = ?1"
        ") "
        "SELECT a.id, a.user_id, a.node_id, a.article_guid, a.title, a.link_url, a.published_at, a.author, "
        "a.content, a.is_read, CASE WHEN q.original_article_id IS NULL THEN 0 ELSE 1 END AS is_queued, "
        "COALESCE(q.original_article_id, '') "
        "FROM articles a "
        "LEFT JOIN queued_articles q ON q.user_id = a.user_id AND q.original_article_id = a.id "
        "WHERE a.user_id = ?1 AND a.node_id IN ("
        "  SELECT id FROM subtree WHERE node_type = ?3"
        ") AND (?4 = '' OR instr(lower(a.title), lower(?4)) > 0) "
        "AND (?5 = 0 OR a.is_read = 0) "
        "ORDER BY a.published_at DESC, a.id DESC LIMIT ?6 OFFSET ?7");
      bindText(*select, 1, user_id);
      bindText(*select, 2, node_id);
      sqlite3_bind_int(select.get(), 3, onerss::pb::TreeNode::TYPE_FEED);
      bindText(*select, 4, title_query);
      sqlite3_bind_int(select.get(), 5, unread_only ? 1 : 0);
      sqlite3_bind_int64(select.get(), 6, static_cast<sqlite3_int64>(limit));
      sqlite3_bind_int64(select.get(), 7, static_cast<sqlite3_int64>(offset));
    } else {
      select = prepare(
        *db_,
        "SELECT a.id, a.user_id, a.node_id, a.article_guid, a.title, a.link_url, a.published_at, a.author, "
        "a.content, a.is_read, CASE WHEN q.original_article_id IS NULL THEN 0 ELSE 1 END AS is_queued, "
        "COALESCE(q.original_article_id, '') "
        "FROM articles a "
        "LEFT JOIN queued_articles q ON q.user_id = a.user_id AND q.original_article_id = a.id "
        "WHERE a.user_id = ?1 AND a.node_id = ?2 "
        "AND (?3 = '' OR instr(lower(a.title), lower(?3)) > 0) "
        "AND (?4 = 0 OR a.is_read = 0) "
        "ORDER BY a.published_at DESC, a.id DESC LIMIT ?5 OFFSET ?6");
      bindText(*select, 1, user_id);
      bindText(*select, 2, node_id);
      bindText(*select, 3, title_query);
      sqlite3_bind_int(select.get(), 4, unread_only ? 1 : 0);
      sqlite3_bind_int64(select.get(), 5, static_cast<sqlite3_int64>(limit));
      sqlite3_bind_int64(select.get(), 6, static_cast<sqlite3_int64>(offset));
    }
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

    auto article = readArticleRecord(*select);
    auto sanitized = sanitizeArticleForStorage(std::move(article));
    if (!sanitized.accepted) {
      LOG_WARN << "Skipping stored article " << sanitized.article.article_id
               << " for node=" << sanitized.article.node_id
               << " reason=" << sanitized.rejection_reason;
      continue;
    }
    articles.push_back(std::move(sanitized.article));
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
  const bool queue_view = node_id == kQueueNodeId;
  statement_ptr update_primary;
  statement_ptr update_queue;
  if (queue_view) {
    update_primary = prepare(*db_,
                             "UPDATE queued_articles SET is_read = 1 "
                             "WHERE user_id = ?1 AND id = ?2 AND is_read = 0");
    update_queue = prepare(*db_,
                           "UPDATE articles SET is_read = 1 "
                           "WHERE user_id = ?1 AND id = ("
                           "  SELECT original_article_id FROM queued_articles WHERE user_id = ?1 AND id = ?2"
                           ") AND is_read = 0");
    bindText(*update_primary, 1, user_id);
    bindText(*update_primary, 2, article_id);
    bindText(*update_queue, 1, user_id);
    bindText(*update_queue, 2, article_id);
  } else {
    update_primary = prepare(*db_,
                             "UPDATE articles SET is_read = 1 "
                             "WHERE user_id = ?1 AND node_id = ?2 AND id = ?3 AND is_read = 0");
    update_queue = prepare(*db_,
                           "UPDATE queued_articles SET is_read = 1 "
                           "WHERE user_id = ?1 AND original_article_id = ?2 AND is_read = 0");
    bindText(*update_primary, 1, user_id);
    bindText(*update_primary, 2, node_id);
    bindText(*update_primary, 3, article_id);
    bindText(*update_queue, 1, user_id);
    bindText(*update_queue, 2, article_id);
  }
  checkStepDone(*db_, sqlite3_step(update_primary.get()));
  const auto changed_primary = sqlite3_changes(db_.get());
  checkStepDone(*db_, sqlite3_step(update_queue.get()));
  const auto changed_queue = sqlite3_changes(db_.get());
  return static_cast<std::size_t>(std::max(changed_primary, changed_queue));
}

std::size_t SqliteTreeRepository::markArticleUnread(const std::string &user_id,
                                                    const std::string &node_id,
                                                    const std::string &article_id) {
  std::scoped_lock lock{mutex_};
  const bool queue_view = node_id == kQueueNodeId;
  statement_ptr update_primary;
  statement_ptr update_queue;
  if (queue_view) {
    update_primary = prepare(*db_,
                             "UPDATE queued_articles SET is_read = 0 "
                             "WHERE user_id = ?1 AND id = ?2 AND is_read = 1");
    update_queue = prepare(*db_,
                           "UPDATE articles SET is_read = 0 "
                           "WHERE user_id = ?1 AND id = ("
                           "  SELECT original_article_id FROM queued_articles WHERE user_id = ?1 AND id = ?2"
                           ") AND is_read = 1");
    bindText(*update_primary, 1, user_id);
    bindText(*update_primary, 2, article_id);
    bindText(*update_queue, 1, user_id);
    bindText(*update_queue, 2, article_id);
  } else {
    update_primary = prepare(*db_,
                             "UPDATE articles SET is_read = 0 "
                             "WHERE user_id = ?1 AND node_id = ?2 AND id = ?3 AND is_read = 1");
    update_queue = prepare(*db_,
                           "UPDATE queued_articles SET is_read = 0 "
                           "WHERE user_id = ?1 AND original_article_id = ?2 AND is_read = 1");
    bindText(*update_primary, 1, user_id);
    bindText(*update_primary, 2, node_id);
    bindText(*update_primary, 3, article_id);
    bindText(*update_queue, 1, user_id);
    bindText(*update_queue, 2, article_id);
  }
  checkStepDone(*db_, sqlite3_step(update_primary.get()));
  const auto changed_primary = sqlite3_changes(db_.get());
  checkStepDone(*db_, sqlite3_step(update_queue.get()));
  const auto changed_queue = sqlite3_changes(db_.get());
  return static_cast<std::size_t>(std::max(changed_primary, changed_queue));
}

std::size_t SqliteTreeRepository::queueArticle(const std::string &user_id,
                                               const std::string &node_id,
                                               const std::string &article_id) {
  std::scoped_lock lock{mutex_};
  if (node_id == kQueueNodeId) {
    return 0;
  }

  auto select = prepare(*db_,
                        "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, "
                        "is_read, 0, '' FROM articles "
                        "WHERE user_id = ?1 AND node_id = ?2 AND id = ?3");
  bindText(*select, 1, user_id);
  bindText(*select, 2, node_id);
  bindText(*select, 3, article_id);
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    throw std::runtime_error{"article not found"};
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }

  auto article = readArticleRecord(*select);
  auto insert = prepare(
    *db_,
    "INSERT INTO queued_articles(id, user_id, original_article_id, node_id, article_guid, title, link_url, "
    "published_at, author, content, is_read) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
    "ON CONFLICT(user_id, original_article_id) DO NOTHING");
  bindText(*insert, 1, newUuid());
  bindText(*insert, 2, user_id);
  bindText(*insert, 3, article.article_id);
  bindText(*insert, 4, article.node_id);
  bindText(*insert, 5, article.guid);
  bindText(*insert, 6, article.title);
  bindText(*insert, 7, article.link_url);
  bindText(*insert, 8, article.published_at);
  bindText(*insert, 9, article.author);
  bindText(*insert, 10, article.content);
  sqlite3_bind_int(insert.get(), 11, article.is_read ? 1 : 0);
  checkStepDone(*db_, sqlite3_step(insert.get()));
  return static_cast<std::size_t>(sqlite3_changes(db_.get()));
}

std::size_t SqliteTreeRepository::unqueueArticle(const std::string &user_id,
                                                 const std::string &node_id,
                                                 const std::string &article_id) {
  std::scoped_lock lock{mutex_};
  statement_ptr remove;
  if (node_id == kQueueNodeId) {
    remove = prepare(*db_, "DELETE FROM queued_articles WHERE user_id = ?1 AND id = ?2");
    bindText(*remove, 1, user_id);
    bindText(*remove, 2, article_id);
  } else {
    remove = prepare(*db_, "DELETE FROM queued_articles WHERE user_id = ?1 AND original_article_id = ?2");
    bindText(*remove, 1, user_id);
    bindText(*remove, 2, article_id);
  }
  checkStepDone(*db_, sqlite3_step(remove.get()));
  return static_cast<std::size_t>(sqlite3_changes(db_.get()));
}

std::size_t SqliteTreeRepository::markAllArticlesRead(const std::string &user_id, const std::string &node_id) {
  std::scoped_lock lock{mutex_};
  statement_ptr update;
  statement_ptr update_queue;
  if (node_id == kQueueNodeId) {
    update = prepare(*db_,
                     "UPDATE queued_articles SET is_read = 1 "
                     "WHERE user_id = ?1 AND is_read = 0");
    update_queue = prepare(*db_,
                           "UPDATE articles SET is_read = 1 "
                           "WHERE user_id = ?1 AND is_read = 0 AND id IN ("
                           "  SELECT original_article_id FROM queued_articles WHERE user_id = ?1"
                           ")");
    bindText(*update, 1, user_id);
    bindText(*update_queue, 1, user_id);
  } else if (node_id.empty()) {
    update = prepare(*db_,
                     "UPDATE articles SET is_read = 1 "
                     "WHERE user_id = ?1 AND is_read = 0");
    bindText(*update, 1, user_id);
  } else {
    const auto node = fetchNodeLocked(user_id, node_id);
    if (node.type == onerss::pb::TreeNode::TYPE_FOLDER) {
      update = prepare(
        *db_,
        "WITH RECURSIVE subtree(id, node_type) AS ("
        "  SELECT id, node_type FROM tree_nodes WHERE user_id = ?1 AND id = ?2"
        "  UNION ALL "
        "  SELECT child.id, child.node_type FROM tree_nodes child "
        "  JOIN subtree parent ON child.parent_id = parent.id "
        "  WHERE child.user_id = ?1"
        ") "
        "UPDATE articles SET is_read = 1 "
        "WHERE user_id = ?1 AND is_read = 0 AND node_id IN ("
        "  SELECT id FROM subtree WHERE node_type = ?3"
        ")");
      bindText(*update, 1, user_id);
      bindText(*update, 2, node_id);
      sqlite3_bind_int(update.get(), 3, onerss::pb::TreeNode::TYPE_FEED);
    } else {
      update = prepare(*db_,
                       "UPDATE articles SET is_read = 1 "
                       "WHERE user_id = ?1 AND node_id = ?2 AND is_read = 0");
      bindText(*update, 1, user_id);
      bindText(*update, 2, node_id);
    }
  }
  checkStepDone(*db_, sqlite3_step(update.get()));
  const auto changed_primary = sqlite3_changes(db_.get());
  if (update_queue) {
    checkStepDone(*db_, sqlite3_step(update_queue.get()));
    return static_cast<std::size_t>(std::max(changed_primary, sqlite3_changes(db_.get())));
  }
  return static_cast<std::size_t>(changed_primary);
}

void SqliteTreeRepository::ensureSchema() {
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS system_kv ("
          "  key TEXT PRIMARY KEY,"
          "  value BLOB NOT NULL"
          ");");
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
          "  archive_mode INTEGER NOT NULL DEFAULT 0,"
          "  archive_limit INTEGER NOT NULL DEFAULT 0,"
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
  try {
    execute(*db_, "ALTER TABLE tree_nodes ADD COLUMN archive_mode INTEGER NOT NULL DEFAULT 0;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  try {
    execute(*db_, "ALTER TABLE tree_nodes ADD COLUMN archive_limit INTEGER NOT NULL DEFAULT 0;");
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
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS queued_articles ("
          "  id TEXT PRIMARY KEY,"
          "  user_id TEXT NOT NULL,"
          "  original_article_id TEXT NOT NULL,"
          "  node_id TEXT NOT NULL,"
          "  article_guid TEXT NOT NULL,"
          "  title TEXT NOT NULL,"
          "  link_url TEXT NOT NULL DEFAULT '',"
          "  published_at TEXT NOT NULL DEFAULT '',"
          "  author TEXT NOT NULL DEFAULT '',"
          "  content TEXT NOT NULL DEFAULT '',"
          "  is_read INTEGER NOT NULL DEFAULT 0,"
          "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
          "  UNIQUE(user_id, original_article_id)"
          ");");
  execute(*db_, "CREATE INDEX IF NOT EXISTS queued_articles_user_idx ON queued_articles(user_id);");
}

void SqliteTreeRepository::migrateDataIfNeeded() {
  const auto current_version = readDataVersion(*db_);
  if (current_version >= kCurrentDataVersion) {
    return;
  }

  LOG_INFO << "Upgrading article data version from " << current_version << " to " << kCurrentDataVersion;
  execute(*db_, "BEGIN IMMEDIATE TRANSACTION;");
  try {
    scanAndSanitizeArticles();
    scanAndSanitizeQueuedArticles();
    writeDataVersion(*db_, kCurrentDataVersion);
    execute(*db_, "COMMIT;");
  } catch (...) {
    try {
      execute(*db_, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
  LOG_INFO << "Article data upgrade completed at version " << kCurrentDataVersion;
}

void SqliteTreeRepository::scanAndSanitizeArticles() {
  auto select = prepare(*db_,
                        "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, "
                        "is_read, 0, '' FROM articles");
  auto update = prepare(
    *db_,
    "UPDATE articles SET article_guid = ?1, title = ?2, link_url = ?3, published_at = ?4, author = ?5, content = ?6 "
    "WHERE id = ?7");
  auto remove = prepare(*db_, "DELETE FROM articles WHERE id = ?1");

  std::vector<ArticleRecord> stored_articles;
  while (true) {
    const auto rc = sqlite3_step(select.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }
    stored_articles.push_back(readArticleRecord(*select));
  }

  std::size_t updated_rows = 0;
  std::size_t deleted_rows = 0;
  std::size_t capped_rows = 0;
  for (auto &article : stored_articles) {
    auto sanitized = sanitizeArticleForStorage(std::move(article));
    if (!sanitized.accepted) {
      sqlite3_reset(remove.get());
      sqlite3_clear_bindings(remove.get());
      bindText(*remove, 1, sanitized.article.article_id);
      checkStepDone(*db_, sqlite3_step(remove.get()));
      ++deleted_rows;
      LOG_WARN << "Removed stored article " << sanitized.article.article_id
               << " for node=" << sanitized.article.node_id
               << " reason=" << sanitized.rejection_reason;
      continue;
    }
    if (!sanitized.modified) {
      continue;
    }

    sqlite3_reset(update.get());
    sqlite3_clear_bindings(update.get());
    bindText(*update, 1, sanitized.article.guid);
    bindText(*update, 2, sanitized.article.title);
    bindText(*update, 3, sanitized.article.link_url);
    bindText(*update, 4, sanitized.article.published_at);
    bindText(*update, 5, sanitized.article.author);
    bindText(*update, 6, sanitized.article.content);
    bindText(*update, 7, sanitized.article.article_id);
    checkStepDone(*db_, sqlite3_step(update.get()));
    ++updated_rows;
    if (sanitized.content_capped) {
      ++capped_rows;
    }
  }

  LOG_INFO << "Sanitized stored articles updated=" << updated_rows
           << " deleted=" << deleted_rows
           << " capped=" << capped_rows;
}

void SqliteTreeRepository::scanAndSanitizeQueuedArticles() {
  auto select = prepare(
    *db_,
    "SELECT id, user_id, node_id, article_guid, title, link_url, published_at, author, content, is_read, 1, "
    "original_article_id FROM queued_articles");
  auto update = prepare(
    *db_,
    "UPDATE queued_articles SET article_guid = ?1, title = ?2, link_url = ?3, published_at = ?4, author = ?5, "
    "content = ?6 WHERE id = ?7");
  auto remove = prepare(*db_, "DELETE FROM queued_articles WHERE id = ?1");

  std::vector<ArticleRecord> stored_articles;
  while (true) {
    const auto rc = sqlite3_step(select.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error{sqlite3_errmsg(db_.get())};
    }
    stored_articles.push_back(readArticleRecord(*select));
  }

  std::size_t updated_rows = 0;
  std::size_t deleted_rows = 0;
  std::size_t capped_rows = 0;
  for (auto &article : stored_articles) {
    auto sanitized = sanitizeArticleForStorage(std::move(article));
    if (!sanitized.accepted) {
      sqlite3_reset(remove.get());
      sqlite3_clear_bindings(remove.get());
      bindText(*remove, 1, sanitized.article.article_id);
      checkStepDone(*db_, sqlite3_step(remove.get()));
      ++deleted_rows;
      continue;
    }
    if (!sanitized.modified) {
      continue;
    }

    sqlite3_reset(update.get());
    sqlite3_clear_bindings(update.get());
    bindText(*update, 1, sanitized.article.guid);
    bindText(*update, 2, sanitized.article.title);
    bindText(*update, 3, sanitized.article.link_url);
    bindText(*update, 4, sanitized.article.published_at);
    bindText(*update, 5, sanitized.article.author);
    bindText(*update, 6, sanitized.article.content);
    bindText(*update, 7, sanitized.article.article_id);
    checkStepDone(*db_, sqlite3_step(update.get()));
    ++updated_rows;
    if (sanitized.content_capped) {
      ++capped_rows;
    }
  }

  LOG_INFO << "Sanitized queued articles updated=" << updated_rows
           << " deleted=" << deleted_rows
           << " capped=" << capped_rows;
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
                        "use_default_refresh_interval, refresh_interval_hours, archive_mode, archive_limit "
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
    .archive_mode = static_cast<onerss::pb::ArchiveMode>(std::clamp(columnInt(*select, 9),
                                                                     static_cast<int>(onerss::pb::ARCHIVE_MODE_USE_DEFAULT),
                                                                     static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED))),
    .archive_limit = static_cast<std::uint32_t>(std::max(0, columnInt(*select, 10))),
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
