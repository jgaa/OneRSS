#include "ssl_util.h"

#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <array>
#include <stdexcept>
#include <string>

namespace onerss::backend {
namespace {

[[noreturn]] void throwLastOpenSslError(const std::string &context) {
  const auto code = ERR_get_error();
  const char *reason = ERR_reason_error_string(code);
  throw std::runtime_error{context + ": " + (reason != nullptr ? reason : "unknown OpenSSL error")};
}

template <typename T>
std::string bioToString(T &writer) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (bio == nullptr) {
    throw std::runtime_error{"Failed to allocate OpenSSL memory BIO"};
  }

  std::unique_ptr<BIO, decltype(&BIO_free)> bio_ptr{bio, &BIO_free};
  if (!writer(*bio_ptr)) {
    throwLastOpenSslError("Failed to convert object to PEM");
  }

  BUF_MEM *buffer = nullptr;
  BIO_get_mem_ptr(bio_ptr.get(), &buffer);
  if (buffer == nullptr || buffer->data == nullptr || buffer->length == 0U) {
    throw std::runtime_error{"OpenSSL returned an empty PEM buffer"};
  }

  return {buffer->data, buffer->length};
}

evp_pkey_ptr generateRsaKey() {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (ctx == nullptr) {
    throwLastOpenSslError("Failed to create RSA key context");
  }

  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx_ptr{ctx, &EVP_PKEY_CTX_free};
  if (EVP_PKEY_keygen_init(ctx_ptr.get()) <= 0) {
    throwLastOpenSslError("Failed to initialize RSA key generation");
  }
  if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx_ptr.get(), 3072) <= 0) {
    throwLastOpenSslError("Failed to set RSA key length");
  }

  EVP_PKEY *raw_key = nullptr;
  if (EVP_PKEY_keygen(ctx_ptr.get(), &raw_key) <= 0 || raw_key == nullptr) {
    throwLastOpenSslError("Failed to generate RSA key");
  }

  return evp_pkey_ptr{raw_key};
}

ASN1_INTEGER *setRandomSerial(X509 &cert) {
  std::array<unsigned char, 16> serial_bytes{};
  if (RAND_bytes(serial_bytes.data(), static_cast<int>(serial_bytes.size())) != 1) {
    throwLastOpenSslError("Failed to generate certificate serial");
  }

  BIGNUM *number = BN_bin2bn(serial_bytes.data(), static_cast<int>(serial_bytes.size()), nullptr);
  if (number == nullptr) {
    throwLastOpenSslError("Failed to build certificate serial bignum");
  }
  std::unique_ptr<BIGNUM, decltype(&BN_free)> number_ptr{number, &BN_free};

  ASN1_INTEGER *serial = BN_to_ASN1_INTEGER(number_ptr.get(), nullptr);
  if (serial == nullptr) {
    throwLastOpenSslError("Failed to convert serial to ASN1 integer");
  }
  if (X509_set_serialNumber(&cert, serial) != 1) {
    ASN1_INTEGER_free(serial);
    throwLastOpenSslError("Failed to assign certificate serial");
  }
  return serial;
}

void addEntry(X509_NAME &name, const char *field, const std::string &value) {
  if (X509_NAME_add_entry_by_txt(&name,
                                 field,
                                 MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char *>(value.c_str()),
                                 -1,
                                 -1,
                                 0)
      != 1) {
    throwLastOpenSslError("Failed to populate X509 subject");
  }
}

void addExtension(X509 &cert, X509 *issuer, const int nid, const std::string &value) {
  X509V3_CTX ctx{};
  X509V3_set_ctx(&ctx, issuer, &cert, nullptr, nullptr, 0);
  X509_EXTENSION *extension = X509V3_EXT_nconf_nid(nullptr, &ctx, nid, value.c_str());
  if (extension == nullptr) {
    throwLastOpenSslError("Failed to create X509 extension");
  }

  std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension_ptr{extension,
                                                                                 &X509_EXTENSION_free};
  if (X509_add_ext(&cert, extension_ptr.get(), -1) != 1) {
    throwLastOpenSslError("Failed to add X509 extension");
  }
}

x509_ptr newCertificate() {
  X509 *cert = X509_new();
  if (cert == nullptr) {
    throwLastOpenSslError("Failed to allocate X509 certificate");
  }
  return x509_ptr{cert};
}

x509_ptr createCertificate(EVP_PKEY &subject_key,
                           X509 *issuer_cert,
                           EVP_PKEY &issuer_key,
                           const std::string &common_name,
                           const bool is_ca,
                           const std::string &extended_key_usage) {
  auto cert = newCertificate();
  if (X509_set_version(cert.get(), 2) != 1) {
    throwLastOpenSslError("Failed to set X509 version");
  }

  ASN1_INTEGER *serial = setRandomSerial(*cert);
  ASN1_INTEGER_free(serial);

  if (X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0) == nullptr) {
    throwLastOpenSslError("Failed to set certificate start time");
  }
  if (X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60L * 24L * 365L * 10L) == nullptr) {
    throwLastOpenSslError("Failed to set certificate expiry");
  }
  if (X509_set_pubkey(cert.get(), &subject_key) != 1) {
    throwLastOpenSslError("Failed to attach public key to certificate");
  }

  X509_NAME *subject = X509_get_subject_name(cert.get());
  if (subject == nullptr) {
    throw std::runtime_error{"Failed to get X509 subject name"};
  }
  addEntry(*subject, "CN", common_name);

  if (issuer_cert != nullptr) {
    if (X509_set_issuer_name(cert.get(), X509_get_subject_name(issuer_cert)) != 1) {
      throwLastOpenSslError("Failed to set issuer name");
    }
  } else if (X509_set_issuer_name(cert.get(), subject) != 1) {
    throwLastOpenSslError("Failed to self-issue certificate");
  }

  addExtension(*cert, issuer_cert != nullptr ? issuer_cert : cert.get(), NID_basic_constraints,
               is_ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
  addExtension(*cert, issuer_cert != nullptr ? issuer_cert : cert.get(), NID_key_usage,
               is_ca ? "critical,keyCertSign,cRLSign"
                     : "critical,digitalSignature,keyEncipherment");
  if (!extended_key_usage.empty()) {
    addExtension(*cert, issuer_cert != nullptr ? issuer_cert : cert.get(), NID_ext_key_usage,
                 extended_key_usage);
  }
  addExtension(*cert, issuer_cert != nullptr ? issuer_cert : cert.get(), NID_subject_key_identifier,
               "hash");
  if (issuer_cert != nullptr) {
    addExtension(*cert, issuer_cert, NID_authority_key_identifier, "keyid:always");
  }

  if (X509_sign(cert.get(), &issuer_key, EVP_sha256()) <= 0) {
    throwLastOpenSslError("Failed to sign certificate");
  }

  return cert;
}

}  // namespace

void EvpPkeyDeleter::operator()(EVP_PKEY *key) const noexcept {
  if (key != nullptr) {
    EVP_PKEY_free(key);
  }
}

void X509Deleter::operator()(X509 *cert) const noexcept {
  if (cert != nullptr) {
    X509_free(cert);
  }
}

void initializeOpenSsl() {
  OPENSSL_init_ssl(0, nullptr);
  OPENSSL_init_crypto(0, nullptr);
}

StoredAuthority generateAuthority() {
  auto ca_key = generateRsaKey();
  auto ca_cert = createCertificate(*ca_key, nullptr, *ca_key, "OneRSS Local CA", true, {});
  auto server_key = generateRsaKey();
  auto server_cert = createCertificate(*server_key,
                                       ca_cert.get(),
                                       *ca_key,
                                       "OneRSS Signup Server",
                                       false,
                                       "serverAuth");

  return {
    .ca_certificate_pem = toPem(*ca_cert),
    .ca_private_key_pem = toPem(*ca_key),
    .server_certificate_pem = toPem(*server_cert),
    .server_private_key_pem = toPem(*server_key),
  };
}

IssuedDeviceCertificate issueClientCertificate(const StoredAuthority &authority,
                                               const std::string &user_id,
                                               const std::string &device_name) {
  auto ca_key = loadPrivateKeyFromPem(authority.ca_private_key_pem);
  auto ca_cert = loadCertificateFromPem(authority.ca_certificate_pem);
  auto client_key = generateRsaKey();
  auto client_cert = createCertificate(*client_key,
                                       ca_cert.get(),
                                       *ca_key,
                                       user_id + " / " + device_name,
                                       false,
                                       "clientAuth");

  return {
    .certificate_pem = toPem(*client_cert),
    .private_key_pem = toPem(*client_key),
  };
}

evp_pkey_ptr loadPrivateKeyFromPem(const std::string &pem) {
  BIO *bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (bio == nullptr) {
    throw std::runtime_error{"Failed to allocate BIO for private key"};
  }

  std::unique_ptr<BIO, decltype(&BIO_free)> bio_ptr{bio, &BIO_free};
  EVP_PKEY *key = PEM_read_bio_PrivateKey(bio_ptr.get(), nullptr, nullptr, nullptr);
  if (key == nullptr) {
    throwLastOpenSslError("Failed to load private key from PEM");
  }
  return evp_pkey_ptr{key};
}

x509_ptr loadCertificateFromPem(const std::string &pem) {
  BIO *bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (bio == nullptr) {
    throw std::runtime_error{"Failed to allocate BIO for certificate"};
  }

  std::unique_ptr<BIO, decltype(&BIO_free)> bio_ptr{bio, &BIO_free};
  X509 *cert = PEM_read_bio_X509(bio_ptr.get(), nullptr, nullptr, nullptr);
  if (cert == nullptr) {
    throwLastOpenSslError("Failed to load certificate from PEM");
  }
  return x509_ptr{cert};
}

std::string toPem(EVP_PKEY &key) {
  auto writer = [&key](BIO &bio) {
    return PEM_write_bio_PrivateKey(&bio, &key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
  };
  return bioToString(writer);
}

std::string toPem(X509 &cert) {
  auto writer = [&cert](BIO &bio) { return PEM_write_bio_X509(&bio, &cert) == 1; };
  return bioToString(writer);
}

}  // namespace onerss::backend
