#include "user_notification.h"

#include <QObject>

namespace onerss::desktop {
namespace {

using UiMessageCode = onerss::pb::UiMessageCodeGadget::UiMessageCode;
using UiMessageSeverity = onerss::pb::UiMessageSeverityGadget::UiMessageSeverity;

QString translatedCode(const UiMessageCode code) {
  switch (code) {
    case UiMessageCode::UI_MESSAGE_CODE_CONNECTED:
      return QObject::tr("Connected to the server.");
    case UiMessageCode::UI_MESSAGE_CODE_PAIRING_REQUIRED:
      return QObject::tr("Pair a device before syncing.");
    case UiMessageCode::UI_MESSAGE_CODE_CONNECTION_FAILED:
      return QObject::tr("Could not connect to the server.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_CREATED:
      return QObject::tr("Item created.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_UPDATED:
      return QObject::tr("Item updated.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_DELETED:
      return QObject::tr("Item deleted.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_CREATE_FAILED:
      return QObject::tr("Could not create the item.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_UPDATE_FAILED:
      return QObject::tr("Could not update the item.");
    case UiMessageCode::UI_MESSAGE_CODE_TREE_NODE_DELETE_FAILED:
      return QObject::tr("Could not delete the item.");
    case UiMessageCode::UI_MESSAGE_CODE_ARTICLES_LOADED:
      return QObject::tr("Articles loaded.");
    case UiMessageCode::UI_MESSAGE_CODE_ARTICLES_LOAD_FAILED:
      return QObject::tr("Could not load articles.");
    case UiMessageCode::UI_MESSAGE_CODE_FEED_REFRESHED:
      return QObject::tr("Feed refreshed.");
    case UiMessageCode::UI_MESSAGE_CODE_FEED_REFRESH_FAILED:
      return QObject::tr("Could not refresh the feed.");
    case UiMessageCode::UI_MESSAGE_CODE_INVALID_REQUEST:
      return QObject::tr("The request could not be processed.");
    case UiMessageCode::UI_MESSAGE_CODE_AUTHENTICATION_FAILED:
      return QObject::tr("Authentication failed.");
    case UiMessageCode::UI_MESSAGE_CODE_SIGNUP_FAILED:
      return QObject::tr("Sign-up or pairing failed.");
    default:
      return QObject::tr("Server message");
  }
}

UiStatusMessage::Severity toSeverity(const UiMessageSeverity severity) {
  switch (severity) {
    case UiMessageSeverity::UI_MESSAGE_SEVERITY_SUCCESS:
      return UiStatusMessage::Severity::Success;
    case UiMessageSeverity::UI_MESSAGE_SEVERITY_WARNING:
      return UiStatusMessage::Severity::Warning;
    case UiMessageSeverity::UI_MESSAGE_SEVERITY_ERROR:
      return UiStatusMessage::Severity::Error;
    case UiMessageSeverity::UI_MESSAGE_SEVERITY_INFO:
    case UiMessageSeverity::UI_MESSAGE_SEVERITY_UNSPECIFIED:
    default:
      return UiStatusMessage::Severity::Info;
  }
}

}  // namespace

UiStatusMessage toUiStatusMessage(const onerss::pb::UserNotification &notification) {
  return UiStatusMessage{
    .severity = toSeverity(notification.severity()),
    .text = translatedCode(notification.code()),
    .detail = notification.context(),
  };
}

}  // namespace onerss::desktop
