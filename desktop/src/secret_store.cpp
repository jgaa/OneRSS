#include "secret_store.h"

#include "logging.h"

#include <safekeeping/SafeKeeping.h>

#include <stdexcept>

namespace onerss::desktop {
namespace {

using jgaa::safekeeping::SafeKeeping;

SafeKeeping::ptr_t openVault() {
  SafeKeeping::setLinuxVaultRootName("org.jgaa.OneRSS");
  SafeKeeping::setLinuxVaultBackend(SafeKeeping::LinuxVaultBackend::Auto);

  SafeKeeping::CreateOptions options;
  options.createSystemVaultSlot = true;
  options.createRecoveryKey = false;
  options.requireAtLeastOneUnlockMethod = true;

  return SafeKeeping::openOrCreate("desktop", options);
}

void storeRequiredSecret(SafeKeeping &vault, std::string_view name, std::string_view secret) {
  if (!vault.storeSecret(name, secret)) {
    throw std::runtime_error{"SafeKeeping storeSecret failed: " + vault.latestError().message};
  }
}

}  // namespace

void SecretStore::initialize() const {
  auto vault = openVault();
  LOG_INFO << "Initialized SafeKeeping namespace '" << vault->namespaceName() << "'";
}

void SecretStore::storeSignupMaterial(const onerss::pb::SignupResponse &response,
                                      const QString &server_host,
                                      const int signup_port) const {
  auto vault = openVault();
  LOG_DEBUG << "Persisting enrollment material for user_id=" << response.user_id()
            << " device_id=" << response.device_id();

  storeRequiredSecret(*vault, "user.id", response.user_id());
  storeRequiredSecret(*vault, "user.login", response.login());
  storeRequiredSecret(*vault, "device.id", response.device_id());
  storeRequiredSecret(*vault, "tls.ca_cert", response.ca_certificate_pem());
  storeRequiredSecret(*vault, "tls.client_cert", response.client_certificate_pem());
  storeRequiredSecret(*vault, "tls.client_key", response.client_private_key_pem());
  storeRequiredSecret(*vault, "server.host", server_host.toStdString());
  storeRequiredSecret(*vault, "server.signup_port", std::to_string(signup_port));
  storeRequiredSecret(*vault, "server.app_port", std::to_string(static_cast<int>(response.app_port())));
}

StoredPeer SecretStore::loadPeer() const {
  auto vault = openVault();

  auto get = [&vault](std::string_view name) -> QString {
    const auto value = vault->retrieveSecret(name);
    return value.has_value() ? QString::fromStdString(*value) : QString{};
  };

  StoredPeer peer;
  peer.user_id = get("user.id");
  peer.login = get("user.login");
  peer.device_id = get("device.id");
  peer.ca_certificate_pem = get("tls.ca_cert");
  peer.client_certificate_pem = get("tls.client_cert");
  peer.client_private_key_pem = get("tls.client_key");
  peer.server_host = get("server.host");
  peer.signup_port = get("server.signup_port").toInt();
  peer.app_port = get("server.app_port").toInt();
  if (peer.app_port <= 0) {
    peer.app_port = 7444;
  }
  if (peer.signup_port <= 0) {
    peer.signup_port = 7443;
  }
  return peer;
}

QString SecretStore::namespaceName() const {
  return QStringLiteral("desktop");
}

}  // namespace onerss::desktop
