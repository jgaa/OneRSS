#pragma once

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

namespace onerss::backend {

struct GeneratedIdentity {
  std::string certificate_pem;
  std::string private_key_pem;
};

struct IssuedDeviceCertificate {
  std::string certificate_pem;
  std::string private_key_pem;
};

struct StoredAuthority {
  std::string ca_certificate_pem;
  std::string ca_private_key_pem;
  std::string server_certificate_pem;
  std::string server_private_key_pem;
};

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY *key) const noexcept;
};

struct X509Deleter {
  void operator()(X509 *cert) const noexcept;
};

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using x509_ptr = std::unique_ptr<X509, X509Deleter>;

void initializeOpenSsl();
StoredAuthority generateAuthority();
IssuedDeviceCertificate issueClientCertificate(const StoredAuthority &authority,
                                               const std::string &user_id,
                                               const std::string &device_name);

evp_pkey_ptr loadPrivateKeyFromPem(const std::string &pem);
x509_ptr loadCertificateFromPem(const std::string &pem);
std::string toPem(EVP_PKEY &key);
std::string toPem(X509 &cert);

}  // namespace onerss::backend
