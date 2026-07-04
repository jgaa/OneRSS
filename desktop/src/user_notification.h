#pragma once

#include "onerss.qpb.h"

#include <QString>

namespace onerss::desktop {

struct UiStatusMessage {
  enum class Severity {
    Info,
    Success,
    Warning,
    Error
  };

  Severity severity = Severity::Info;
  QString text;
  QString detail;
};

UiStatusMessage toUiStatusMessage(const onerss::pb::UserNotification &notification);

}  // namespace onerss::desktop
