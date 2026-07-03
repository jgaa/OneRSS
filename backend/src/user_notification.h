#pragma once

#include "onerss.pb.h"

#include <string>

namespace onerss::backend {

onerss::pb::UserNotification makeNotification(onerss::pb::UiMessageSeverity severity,
                                              onerss::pb::UiMessageCode code,
                                              std::string context = {});

void attachNotification(onerss::pb::OutgoingEnvelope &envelope,
                        onerss::pb::UiMessageSeverity severity,
                        onerss::pb::UiMessageCode code,
                        std::string context = {});

void attachError(onerss::pb::OutgoingEnvelope &envelope,
                 std::string error_code,
                 std::string error_message,
                 onerss::pb::UiMessageCode ui_code,
                 onerss::pb::UiMessageSeverity severity,
                 std::string context = {});

}  // namespace onerss::backend
