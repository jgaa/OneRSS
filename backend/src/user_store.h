#pragma once

#include "sqlite.h"
#include "ssl_util.h"
#include "onerss.pb.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace onerss::backend {

struct SignupResult {
  std::string user_id;
  std::string login;
  std::string device_id;
  bool created_account = false;
  IssuedDeviceCertificate client_identity;
};

struct DeviceIdentity {
  std::string user_id;
  std::string login;
  std::string device_id;
};

struct UserSettingsRecord {
  std::uint32_t default_refresh_interval_hours = 12;
  onerss::pb::ArchiveMode default_archive_mode = onerss::pb::ARCHIVE_MODE_KEEP_ALL;
  std::uint32_t default_archive_limit = 0;
};

class UserStore final {
 public:
  explicit UserStore(const std::filesystem::path &database_path);

  [[nodiscard]] const StoredAuthority &authority() const noexcept;
  SignupResult signupOrPair(bool create_account,
                            const std::string &login,
                            const std::string &password,
                            const std::string &device_name);
  [[nodiscard]] DeviceIdentity authenticateDeviceCertificate(const std::string &certificate_pem);
  [[nodiscard]] UserSettingsRecord getUserSettings(const std::string &user_id);
  [[nodiscard]] UserSettingsRecord updateUserSettings(const std::string &user_id,
                                                      const UserSettingsRecord &settings);

 private:
  struct UserRecord {
    std::string user_id;
    std::string login;
    std::string salt;
    std::string password_hash;
  };

  void ensureSchema();
  void ensureAuthority();
  [[nodiscard]] std::optional<UserRecord> findUserLocked(const std::string &login);
  [[nodiscard]] static std::string hashPassword(const std::string &password, const std::string &salt);
  [[nodiscard]] static std::string randomBytes(std::size_t size);
  [[nodiscard]] static std::string newUuid();
  static void bindBlob(sqlite3_stmt &statement, int index, const std::string &value);
  static void bindText(sqlite3_stmt &statement, int index, const std::string &value);

  sqlite_ptr db_;
  StoredAuthority authority_;
  mutable std::mutex mutex_;
};

}  // namespace onerss::backend
