#include "signup_client.h"

#include "logging.h"

#include <arpa/inet.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace onerss::desktop {
namespace {

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using onerss::pb::IncomingEnvelope;
using onerss::pb::OutgoingEnvelope;

std::string newUuid() {
  return boost::uuids::to_string(boost::uuids::random_generator()());
}

void writeEnvelope(ssl::stream<tcp::socket> &stream, const IncomingEnvelope &envelope) {
  std::string payload;
  if (!envelope.SerializeToString(&payload)) {
    throw std::runtime_error{"Failed to serialize signup request"};
  }

  const auto frame_size = htonl(static_cast<std::uint32_t>(payload.size()));
  boost::asio::write(stream, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  boost::asio::write(stream, boost::asio::buffer(payload));
}

OutgoingEnvelope readEnvelope(ssl::stream<tcp::socket> &stream) {
  std::array<std::byte, 4> size_buffer{};
  boost::asio::read(stream, boost::asio::buffer(size_buffer));
  std::uint32_t frame_size_network = 0;
  std::memcpy(&frame_size_network, size_buffer.data(), sizeof(frame_size_network));
  const auto frame_size = ntohl(frame_size_network);
  if (frame_size == 0 || frame_size > 1024 * 1024) {
    throw std::runtime_error{"Received invalid frame size from server"};
  }

  std::vector<char> payload(frame_size);
  boost::asio::read(stream, boost::asio::buffer(payload));
  OutgoingEnvelope response;
  if (!response.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    throw std::runtime_error{"Failed to parse signup response"};
  }
  return response;
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
  request_envelope.set_request_id(newUuid());
  auto *payload = request_envelope.mutable_signup_request();
  payload->set_mode(create_account ? onerss::pb::SignupRequest::MODE_CREATE_ACCOUNT
                                   : onerss::pb::SignupRequest::MODE_PAIR_DEVICE);
  payload->set_login(login);
  payload->set_password(password);
  payload->set_device_name(device_name);

  writeEnvelope(stream, request_envelope);
  const auto response = readEnvelope(stream);
  if (response.has_error()) {
    throw std::runtime_error(std::string{response.error().code() + ": " + response.error().message()});
  }
  if (!response.has_signup_response()) {
    throw std::runtime_error{"Server returned an unexpected response"};
  }

  LOG_INFO << "Signup completed for user_id=" << response.signup_response().user_id()
           << " device_id=" << response.signup_response().device_id();
  stream.shutdown();
  return response.signup_response();
}

}  // namespace onerss::desktop
