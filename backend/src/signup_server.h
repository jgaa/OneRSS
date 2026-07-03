#pragma once

#include "user_store.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <cstdint>
#include <string>

namespace onerss::backend {

class SignupServer final {
 public:
  SignupServer(UserStore &user_store,
               std::string bind_address,
               std::uint16_t port,
               std::uint16_t app_port);
  void run();

 private:
  using tcp = boost::asio::ip::tcp;
  using ssl_stream_t = boost::asio::ssl::stream<tcp::socket>;

  void handleClient(tcp::socket socket);

  UserStore &user_store_;
  std::string bind_address_;
  std::uint16_t port_;
  std::uint16_t app_port_;
};

}  // namespace onerss::backend
