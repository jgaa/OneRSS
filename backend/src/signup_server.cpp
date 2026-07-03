#include "signup_server.h"

#include "logging.h"

#include "onerss.pb.h"
#include "user_notification.h"

#include <arpa/inet.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace onerss::backend {
namespace {

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using onerss::pb::IncomingEnvelope;
using onerss::pb::OutgoingEnvelope;

IncomingEnvelope readEnvelope(ssl::stream<tcp::socket> &stream) {
  std::array<std::byte, 4> size_buffer{};
  boost::asio::read(stream, boost::asio::buffer(size_buffer));
  std::uint32_t frame_size_network = 0;
  std::memcpy(&frame_size_network, size_buffer.data(), sizeof(frame_size_network));
  const auto frame_size = ntohl(frame_size_network);
  if (frame_size == 0 || frame_size > 1024 * 1024) {
    throw std::runtime_error{"invalid frame size"};
  }

  std::vector<char> payload(frame_size);
  boost::asio::read(stream, boost::asio::buffer(payload));

  IncomingEnvelope envelope;
  if (!envelope.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    throw std::runtime_error{"failed to parse protobuf envelope"};
  }
  return envelope;
}

void writeEnvelope(ssl::stream<tcp::socket> &stream, const OutgoingEnvelope &envelope) {
  std::string payload;
  if (!envelope.SerializeToString(&payload)) {
    throw std::runtime_error{"failed to serialize protobuf envelope"};
  }

  const auto frame_size = htonl(static_cast<std::uint32_t>(payload.size()));
  boost::asio::write(stream, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  boost::asio::write(stream, boost::asio::buffer(payload));
}

ssl::context createSslContext(const StoredAuthority &authority) {
  ssl::context context{ssl::context::tls_server};
  context.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2
                      | ssl::context::no_sslv3 | ssl::context::single_dh_use);
  context.use_certificate_chain(
    boost::asio::buffer(authority.server_certificate_pem.data(), authority.server_certificate_pem.size()));
  context.use_private_key(
    boost::asio::buffer(authority.server_private_key_pem.data(), authority.server_private_key_pem.size()),
    ssl::context::pem);
  return context;
}

}  // namespace

SignupServer::SignupServer(UserStore &user_store,
                           std::string bind_address,
                           const std::uint16_t port,
                           const std::uint16_t app_port)
    : user_store_{user_store}, bind_address_{std::move(bind_address)}, port_{port}, app_port_{app_port} {}

void SignupServer::run() {
  boost::asio::io_context io_context;
  tcp::endpoint endpoint{boost::asio::ip::make_address(bind_address_), port_};
  tcp::acceptor acceptor{io_context, endpoint};

  LOG_INFO << "Signup server listening on " << bind_address_ << ":" << port_;
  for (;;) {
    tcp::socket socket{io_context};
    acceptor.accept(socket);
    LOG_DEBUG << "Accepted signup connection from " << socket.remote_endpoint();
    handleClient(std::move(socket));
  }
}

void SignupServer::handleClient(tcp::socket socket) {
  try {
    boost::asio::ssl::context context = createSslContext(user_store_.authority());
    ssl_stream_t stream{std::move(socket), context};
    stream.handshake(ssl_stream_t::server);
    LOG_TRACE << "TLS handshake completed";

    const auto request = readEnvelope(stream);
    OutgoingEnvelope response;
    response.set_request_id(request.request_id());

    try {
      if (!request.has_signup_request()) {
        throw std::runtime_error{"unsupported request type"};
      }

      const auto &signup = request.signup_request();
      LOG_DEBUG << "Received signup envelope request_id=" << request.request_id() << " login=" << signup.login();

      const auto result = user_store_.signupOrPair(
        signup.mode() == onerss::pb::SignupRequest::MODE_CREATE_ACCOUNT,
        signup.login(),
        signup.password(),
        signup.device_name());

      auto *payload = response.mutable_signup_response();
      payload->set_user_id(result.user_id);
      payload->set_login(result.login);
      payload->set_device_id(result.device_id);
      payload->set_created_account(result.created_account);
      payload->set_ca_certificate_pem(user_store_.authority().ca_certificate_pem);
      payload->set_client_certificate_pem(result.client_identity.certificate_pem);
      payload->set_client_private_key_pem(result.client_identity.private_key_pem);
      payload->set_app_port(app_port_);
      attachNotification(response,
                         onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                         onerss::pb::UI_MESSAGE_CODE_CONNECTED,
                         "signup completed");
      LOG_INFO << "Signup request completed for user_id=" << result.user_id << " device_id=" << result.device_id;
    } catch (const std::exception &error) {
      attachError(response,
                  "signup_failed",
                  error.what(),
                  onerss::pb::UI_MESSAGE_CODE_SIGNUP_FAILED,
                  onerss::pb::UI_MESSAGE_SEVERITY_ERROR,
                  error.what());
      LOG_WARN << "Signup request failed after parsing envelope: " << error.what();
    }

    writeEnvelope(stream, response);
    stream.shutdown();
  } catch (const std::exception &error) {
    LOG_WARN << "Signup request failed: " << error.what();
  }
}

}  // namespace onerss::backend
