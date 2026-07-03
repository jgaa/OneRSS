#include "user_notification.h"

namespace onerss::backend {

onerss::pb::UserNotification makeNotification(const onerss::pb::UiMessageSeverity severity,
                                              const onerss::pb::UiMessageCode code,
                                              std::string context) {
  onerss::pb::UserNotification notification;
  notification.set_severity(severity);
  notification.set_code(code);
  notification.set_context(std::move(context));
  return notification;
}

void attachNotification(onerss::pb::OutgoingEnvelope &envelope,
                        const onerss::pb::UiMessageSeverity severity,
                        const onerss::pb::UiMessageCode code,
                        std::string context) {
  *envelope.mutable_user_notification() = makeNotification(severity, code, std::move(context));
}

void attachError(onerss::pb::OutgoingEnvelope &envelope,
                 std::string error_code,
                 std::string error_message,
                 const onerss::pb::UiMessageCode ui_code,
                 const onerss::pb::UiMessageSeverity severity,
                 std::string context) {
  auto *error = envelope.mutable_error();
  error->set_code(std::move(error_code));
  error->set_message(std::move(error_message));
  *error->mutable_user_notification() = makeNotification(severity, ui_code, std::move(context));
  *envelope.mutable_user_notification() = error->user_notification();
}

}  // namespace onerss::backend
