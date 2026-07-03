#pragma once

#include "onerss.pb.h"

#include <QString>

namespace onerss::desktop {

struct StoredPeer final {
  QString user_id;
  QString login;
  QString device_id;
  QString ca_certificate_pem;
  QString client_certificate_pem;
  QString client_private_key_pem;
  QString server_host;
  int signup_port = 7443;
  int app_port = 7444;

  [[nodiscard]] bool isValid() const {
    return !device_id.isEmpty() && !ca_certificate_pem.isEmpty() && !client_certificate_pem.isEmpty()
           && !client_private_key_pem.isEmpty() && !server_host.isEmpty();
  }
};

class SecretStore final {
 public:
  void initialize() const;
  void storeSignupMaterial(const onerss::pb::SignupResponse &response,
                           const QString &server_host,
                           int signup_port) const;
  [[nodiscard]] StoredPeer loadPeer() const;
  [[nodiscard]] QString namespaceName() const;
};

}  // namespace onerss::desktop
