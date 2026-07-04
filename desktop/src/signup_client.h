#pragma once

#include "onerss.qpb.h"

#include <cstdint>
#include <string>

namespace onerss::desktop {

class SignupClient final {
 public:
  [[nodiscard]] onerss::pb::SignupResponse signupOrPair(const std::string &host,
                                                        std::uint16_t port,
                                                        bool create_account,
                                                        const std::string &login,
                                                        const std::string &password,
                                                        const std::string &device_name) const;
};

}  // namespace onerss::desktop
