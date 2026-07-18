#include "user_store.h"

#include "logging.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sqlite3.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>

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

std::string columnBlob(sqlite3_stmt &statement, const int index) {
  const auto *data = static_cast<const char *>(sqlite3_column_blob(&statement, index));
  const auto size = sqlite3_column_bytes(&statement, index);
  return data != nullptr && size > 0 ? std::string{data, static_cast<std::size_t>(size)} : std::string{};
}

std::string columnText(sqlite3_stmt &statement, const int index) {
  const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(&statement, index));
  return data != nullptr ? std::string{data} : std::string{};
}

void checkStepDone(sqlite3 &db, const int rc) {
  if (rc != SQLITE_DONE) {
    throw std::runtime_error{sqlite3_errmsg(&db)};
  }
}

}  // namespace

UserStore::UserStore(const std::filesystem::path &database_path) : db_{openDatabase(database_path)} {
  LOG_INFO << "Opened backend database at " << database_path;
  ensureSchema();
  ensureAuthority();
}

const StoredAuthority &UserStore::authority() const noexcept {
  return authority_;
}

SignupResult UserStore::signupOrPair(const bool create_account,
                                     const std::string &login,
                                     const std::string &password,
                                     const std::string &device_name) {
  std::scoped_lock lock{mutex_};
  LOG_DEBUG << "Processing signup request for login=" << login << " device=" << device_name
            << " create_account=" << create_account;

  if (login.empty() || password.empty() || device_name.empty()) {
    throw std::runtime_error{"login, password, and device name are required"};
  }

  auto user = findUserLocked(login);
  bool created_account = false;

  if (!user.has_value()) {
    if (!create_account) {
      throw std::runtime_error{"unknown account"};
    }

    const auto user_id = newUuid();
    const auto salt = randomBytes(16);
    const auto password_hash = hashPassword(password, salt);

    auto insert_user = prepare(*db_,
                               "INSERT INTO users(id, login, password_salt, password_hash) "
                               "VALUES(?1, ?2, ?3, ?4)");
    bindText(*insert_user, 1, user_id);
    bindText(*insert_user, 2, login);
    bindBlob(*insert_user, 3, salt);
    bindBlob(*insert_user, 4, password_hash);
    checkStepDone(*db_, sqlite3_step(insert_user.get()));

    user = UserRecord{.user_id = user_id, .login = login, .salt = salt, .password_hash = password_hash};
    created_account = true;
    LOG_INFO << "Created user account " << user_id << " for login=" << login;
  } else {
    const auto expected_hash = hashPassword(password, user->salt);
    if (expected_hash.size() != user->password_hash.size()
        || CRYPTO_memcmp(expected_hash.data(), user->password_hash.data(), expected_hash.size()) != 0) {
      LOG_WARN << "Rejected pairing attempt for login=" << login << " due to bad password";
      throw std::runtime_error{"invalid credentials"};
    }
    if (create_account) {
      throw std::runtime_error{"account already exists"};
    }
    LOG_INFO << "Password verified for existing user " << user->user_id;
  }

  const auto device_id = newUuid();
  auto client_identity = issueClientCertificate(authority_, user->user_id, device_name);

  auto insert_device = prepare(
    *db_,
    "INSERT INTO devices(id, user_id, device_name, client_certificate_pem) VALUES(?1, ?2, ?3, ?4)");
  bindText(*insert_device, 1, device_id);
  bindText(*insert_device, 2, user->user_id);
  bindText(*insert_device, 3, device_name);
  bindText(*insert_device, 4, client_identity.certificate_pem);
  checkStepDone(*db_, sqlite3_step(insert_device.get()));

  LOG_INFO << "Issued device certificate for user=" << user->user_id << " device_id=" << device_id;
  LOG_TRACE << "Certificate sizes: cert=" << client_identity.certificate_pem.size()
            << " key=" << client_identity.private_key_pem.size();

  return SignupResult{
    .user_id = user->user_id,
    .login = user->login,
    .device_id = device_id,
    .created_account = created_account,
    .client_identity = std::move(client_identity),
  };
}

DeviceIdentity UserStore::authenticateDeviceCertificate(const std::string &certificate_pem) {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_,
                        "SELECT devices.user_id, users.login, devices.id "
                        "FROM devices "
                        "JOIN users ON users.id = devices.user_id "
                        "WHERE devices.client_certificate_pem = ?1");
  bindText(*select, 1, certificate_pem);
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    throw std::runtime_error{"unknown client certificate"};
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }

  return DeviceIdentity{
    .user_id = columnText(*select, 0),
    .login = columnText(*select, 1),
    .device_id = columnText(*select, 2),
  };
}

UserSettingsRecord UserStore::getUserSettings(const std::string &user_id) {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_,
                        "SELECT default_refresh_interval_hours, default_archive_mode, default_archive_limit "
                        "FROM users WHERE id = ?1");
  bindText(*select, 1, user_id);
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    throw std::runtime_error{"user not found"};
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }
  return UserSettingsRecord{
    .default_refresh_interval_hours
    = static_cast<std::uint32_t>(std::max(1, sqlite3_column_int(select.get(), 0))),
    .default_archive_mode = static_cast<onerss::pb::ArchiveMode>(std::clamp(sqlite3_column_int(select.get(), 1),
                                                                             static_cast<int>(onerss::pb::ARCHIVE_MODE_KEEP_ALL),
                                                                             static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED))),
    .default_archive_limit = static_cast<std::uint32_t>(std::max(0, sqlite3_column_int(select.get(), 2))),
  };
}

UserSettingsRecord UserStore::updateUserSettings(const std::string &user_id, const UserSettingsRecord &settings) {
  std::scoped_lock lock{mutex_};
  const auto archive_mode = std::clamp(static_cast<int>(settings.default_archive_mode),
                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_KEEP_ALL),
                                       static_cast<int>(onerss::pb::ARCHIVE_MODE_DISABLED));
  auto update = prepare(*db_,
                        "UPDATE users SET default_refresh_interval_hours = ?1, default_archive_mode = ?2, "
                        "default_archive_limit = ?3 WHERE id = ?4");
  sqlite3_bind_int(update.get(), 1, static_cast<int>(std::max<std::uint32_t>(1, settings.default_refresh_interval_hours)));
  sqlite3_bind_int(update.get(), 2, archive_mode);
  sqlite3_bind_int(update.get(), 3, static_cast<int>(std::max<std::uint32_t>(1, settings.default_archive_limit)));
  bindText(*update, 4, user_id);
  checkStepDone(*db_, sqlite3_step(update.get()));
  if (sqlite3_changes(db_.get()) == 0) {
    throw std::runtime_error{"user not found"};
  }
  return UserSettingsRecord{
    .default_refresh_interval_hours = std::max<std::uint32_t>(1, settings.default_refresh_interval_hours),
    .default_archive_mode = static_cast<onerss::pb::ArchiveMode>(archive_mode),
    .default_archive_limit = std::max<std::uint32_t>(1, settings.default_archive_limit),
  };
}

void UserStore::ensureSchema() {
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS system_kv ("
          "  key TEXT PRIMARY KEY,"
          "  value BLOB NOT NULL"
          ");");
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS users ("
          "  id TEXT PRIMARY KEY,"
          "  login TEXT NOT NULL UNIQUE,"
          "  default_refresh_interval_hours INTEGER NOT NULL DEFAULT 12,"
          "  default_archive_mode INTEGER NOT NULL DEFAULT 1,"
          "  default_archive_limit INTEGER NOT NULL DEFAULT 1,"
          "  password_salt BLOB NOT NULL,"
          "  password_hash BLOB NOT NULL"
          ");");
  try {
    execute(*db_, "ALTER TABLE users ADD COLUMN default_refresh_interval_hours INTEGER NOT NULL DEFAULT 12;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  try {
    execute(*db_, "ALTER TABLE users ADD COLUMN default_archive_mode INTEGER NOT NULL DEFAULT 1;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  try {
    execute(*db_, "ALTER TABLE users ADD COLUMN default_archive_limit INTEGER NOT NULL DEFAULT 1;");
  } catch (const std::exception &error) {
    if (std::string_view{error.what()}.find("duplicate column name") == std::string_view::npos) {
      throw;
    }
  }
  execute(*db_,
          "CREATE TABLE IF NOT EXISTS devices ("
          "  id TEXT PRIMARY KEY,"
          "  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
          "  device_name TEXT NOT NULL,"
          "  client_certificate_pem TEXT NOT NULL"
          ");");
}

void UserStore::ensureAuthority() {
  std::scoped_lock lock{mutex_};
  auto select = prepare(*db_, "SELECT key, value FROM system_kv");
  while (sqlite3_step(select.get()) == SQLITE_ROW) {
    const auto key = columnText(*select, 0);
    const auto value = columnBlob(*select, 1);
    if (key == "ca_certificate_pem") {
      authority_.ca_certificate_pem = value;
    } else if (key == "ca_private_key_pem") {
      authority_.ca_private_key_pem = value;
    } else if (key == "server_certificate_pem") {
      authority_.server_certificate_pem = value;
    } else if (key == "server_private_key_pem") {
      authority_.server_private_key_pem = value;
    }
  }

  if (!authority_.ca_certificate_pem.empty() && !authority_.ca_private_key_pem.empty()
      && !authority_.server_certificate_pem.empty() && !authority_.server_private_key_pem.empty()) {
    LOG_INFO << "Loaded persisted CA and server certificate material from SQLite";
    return;
  }

  authority_ = generateAuthority();
  LOG_INFO << "Generated new CA and server certificate material";

  const std::array<std::pair<const char *, const std::string *>, 4> items{{
    {"ca_certificate_pem", &authority_.ca_certificate_pem},
    {"ca_private_key_pem", &authority_.ca_private_key_pem},
    {"server_certificate_pem", &authority_.server_certificate_pem},
    {"server_private_key_pem", &authority_.server_private_key_pem},
  }};

  for (const auto &[key, value] : items) {
    auto insert = prepare(*db_, "INSERT OR REPLACE INTO system_kv(key, value) VALUES(?1, ?2)");
    bindText(*insert, 1, key);
    bindBlob(*insert, 2, *value);
    checkStepDone(*db_, sqlite3_step(insert.get()));
  }
}

std::optional<UserStore::UserRecord> UserStore::findUserLocked(const std::string &login) {
  auto select = prepare(*db_,
                        "SELECT id, login, password_salt, password_hash "
                        "FROM users WHERE login = ?1");
  bindText(*select, 1, login);
  const auto rc = sqlite3_step(select.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error{sqlite3_errmsg(db_.get())};
  }

  return UserRecord{
    .user_id = columnText(*select, 0),
    .login = columnText(*select, 1),
    .salt = columnBlob(*select, 2),
    .password_hash = columnBlob(*select, 3),
  };
}

std::string UserStore::hashPassword(const std::string &password, const std::string &salt) {
  std::array<unsigned char, 32> output{};
  if (PKCS5_PBKDF2_HMAC(password.c_str(),
                        static_cast<int>(password.size()),
                        reinterpret_cast<const unsigned char *>(salt.data()),
                        static_cast<int>(salt.size()),
                        200000,
                        EVP_sha256(),
                        static_cast<int>(output.size()),
                        output.data())
      != 1) {
    throw std::runtime_error{"PKCS5_PBKDF2_HMAC failed"};
  }
  return {reinterpret_cast<const char *>(output.data()), output.size()};
}

std::string UserStore::randomBytes(const std::size_t size) {
  std::string bytes(size, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), static_cast<int>(bytes.size())) != 1) {
    throw std::runtime_error{"RAND_bytes failed"};
  }
  return bytes;
}

std::string UserStore::newUuid() {
  return boost::uuids::to_string(boost::uuids::random_generator()());
}

void UserStore::bindBlob(sqlite3_stmt &statement, const int index, const std::string &value) {
  if (sqlite3_bind_blob(&statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)
      != SQLITE_OK) {
    throw std::runtime_error{"sqlite3_bind_blob failed"};
  }
}

void UserStore::bindText(sqlite3_stmt &statement, const int index, const std::string &value) {
  if (sqlite3_bind_text(&statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT)
      != SQLITE_OK) {
    throw std::runtime_error{"sqlite3_bind_text failed"};
  }
}

}  // namespace onerss::backend
