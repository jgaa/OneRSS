#pragma once

#include <sqlite3.h>

#include <filesystem>
#include <memory>
#include <string_view>

namespace onerss::backend {

struct SqliteCloser {
  void operator()(sqlite3 *db) const noexcept;
};

using sqlite_ptr = std::unique_ptr<sqlite3, SqliteCloser>;

sqlite_ptr openDatabase(const std::filesystem::path &path);
void execute(sqlite3 &db, std::string_view sql);

}  // namespace onerss::backend
