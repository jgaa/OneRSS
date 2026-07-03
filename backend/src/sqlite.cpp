#include "sqlite.h"

#include <stdexcept>
#include <string>

namespace onerss::backend {

void SqliteCloser::operator()(sqlite3 *db) const noexcept {
  if (db != nullptr) {
    sqlite3_close(db);
  }
}

sqlite_ptr openDatabase(const std::filesystem::path &path) {
  sqlite3 *raw_db = nullptr;
  const auto rc = sqlite3_open_v2(path.string().c_str(),
                                  &raw_db,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                  nullptr);
  if (rc != SQLITE_OK) {
    const auto message = raw_db != nullptr ? sqlite3_errmsg(raw_db) : "sqlite open failed";
    if (raw_db != nullptr) {
      sqlite3_close(raw_db);
    }
    throw std::runtime_error{message};
  }

  sqlite_ptr db{raw_db};
  execute(*db, "PRAGMA foreign_keys = ON;");
  execute(*db, "PRAGMA journal_mode = WAL;");
  execute(*db, "PRAGMA synchronous = NORMAL;");
  return db;
}

void execute(sqlite3 &db, const std::string_view sql) {
  char *error = nullptr;
  const auto rc = sqlite3_exec(&db, std::string{sql}.c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error != nullptr ? error : sqlite3_errmsg(&db);
    sqlite3_free(error);
    throw std::runtime_error{message};
  }
}

}  // namespace onerss::backend
