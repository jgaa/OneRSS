#include "signup_client.h"

#include "logging.h"
#include "protocol_io.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <stdexcept>
#include <string>

namespace onerss::desktop {
namespace {

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using onerss::pb::IncomingEnvelope;
using onerss::pb::OutgoingEnvelope;

QString newUuid() {
  return QString::fromStdString(boost::uuids::to_string(boost::uuids::random_generator()()));
}

}  // namespace

onerss::pb::SignupResponse SignupClient::signupOrPair(const std::string &host,
                                                      const std::uint16_t port,
                                                      const bool create_account,
                                                      const std::string &login,
                                                      const std::string &password,
                                                      const std::string &device_name) const {
  LOG_DEBUG << "Connecting to signup server at " << host << ":" << port;

  boost::asio::io_context io_context;
  ssl::context context{ssl::context::tls_client};
  context.set_verify_mode(ssl::verify_none);

  ssl::stream<tcp::socket> stream{io_context, context};
  tcp::resolver resolver{io_context};
  const auto endpoints = resolver.resolve(host, std::to_string(port));
  boost::asio::connect(stream.next_layer(), endpoints);
  stream.handshake(ssl::stream_base::client);
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

  writeEnvelope<ssl::stream<tcp::socket>, IncomingEnvelope>(stream, request_envelope);
  const auto response = readEnvelope<ssl::stream<tcp::socket>, OutgoingEnvelope>(stream);
  if (response.hasError()) {
    throw std::runtime_error(
      QStringLiteral("%1: %2").arg(response.error().code(), response.error().message()).toStdString());
  }
  if (!response.hasSignupResponse()) {
    throw std::runtime_error{"Server returned an unexpected response"};
  }

  LOG_INFO << "Signup completed for user_id=" << response.signupResponse().userId().toStdString()
           << " device_id=" << response.signupResponse().deviceId().toStdString();
  stream.shutdown();
  return response.signupResponse();
}

}  // namespace onerss::desktop
