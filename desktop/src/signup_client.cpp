#include "signup_client.h"

#include "logging.h"
#include "protocol_io.h"

#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QUuid>

#include <stdexcept>
#include <string>

namespace onerss::desktop {
namespace {
using onerss::pb::IncomingEnvelope;
using onerss::pb::OutgoingEnvelope;

QString newUuid() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace

onerss::pb::SignupResponse SignupClient::signupOrPair(const std::string &host,
                                                      const std::uint16_t port,
                                                      const bool create_account,
                                                      const std::string &login,
                                                      const std::string &password,
                                                      const std::string &device_name) const {
  LOG_DEBUG << "Connecting to signup server at " << host << ":" << port;

  QSslSocket socket;
  QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
  configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  socket.setSslConfiguration(configuration);
  socket.connectToHostEncrypted(QString::fromStdString(host), port);
  if (!socket.waitForEncrypted(30000)) {
    throw std::runtime_error(socket.errorString().toStdString());
  }
  LOG_TRACE << "TLS handshake completed with signup server";

  IncomingEnvelope request_envelope;
  request_envelope.setRequestId(newUuid());
  onerss::pb::SignupRequest payload;
  payload.setMode(create_account ? onerss::pb::SignupRequest::Mode::MODE_CREATE_ACCOUNT
                                 : onerss::pb::SignupRequest::Mode::MODE_PAIR_DEVICE);
  payload.setLogin(QString::fromStdString(login));
  payload.setPassword(QString::fromStdString(password));
  payload.setDeviceName(QString::fromStdString(device_name));
  request_envelope.setSignupRequest(std::move(payload));

  writeEnvelope<QSslSocket, IncomingEnvelope>(socket, request_envelope);
  const auto response = readEnvelope<QSslSocket, OutgoingEnvelope>(socket);
  if (response.hasError()) {
    throw std::runtime_error(
      QStringLiteral("%1: %2").arg(response.error().code(), response.error().message()).toStdString());
  }
  if (!response.hasSignupResponse()) {
    throw std::runtime_error{"Server returned an unexpected response"};
  }

  LOG_INFO << "Signup completed for user_id=" << response.signupResponse().userId().toStdString()
           << " device_id=" << response.signupResponse().deviceId().toStdString();
  socket.disconnectFromHost();
  return response.signupResponse();
}

}  // namespace onerss::desktop
