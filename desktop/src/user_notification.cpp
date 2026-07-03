#include "user_notification.h"

#include <QObject>

namespace onerss::desktop {
namespace {

QString translatedCode(const onerss::pb::UiMessageCode code) {
  switch (code) {
    case onerss::pb::UI_MESSAGE_CODE_CONNECTED:
      return QObject::tr("Connected to the server.");
    case onerss::pb::UI_MESSAGE_CODE_PAIRING_REQUIRED:
      return QObject::tr("Pair a device before syncing.");
    case onerss::pb::UI_MESSAGE_CODE_CONNECTION_FAILED:
      return QObject::tr("Could not connect to the server.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_CREATED:
      return QObject::tr("Item created.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_UPDATED:
      return QObject::tr("Item updated.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_DELETED:
      return QObject::tr("Item deleted.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_CREATE_FAILED:
      return QObject::tr("Could not create the item.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_UPDATE_FAILED:
      return QObject::tr("Could not update the item.");
    case onerss::pb::UI_MESSAGE_CODE_TREE_NODE_DELETE_FAILED:
      return QObject::tr("Could not delete the item.");
    case onerss::pb::UI_MESSAGE_CODE_ARTICLES_LOADED:
      return QObject::tr("Articles loaded.");
    case onerss::pb::UI_MESSAGE_CODE_ARTICLES_LOAD_FAILED:
      return QObject::tr("Could not load articles.");
    case onerss::pb::UI_MESSAGE_CODE_FEED_REFRESHED:
      return QObject::tr("Feed refreshed.");
    case onerss::pb::UI_MESSAGE_CODE_FEED_REFRESH_FAILED:
      return QObject::tr("Could not refresh the feed.");
    case onerss::pb::UI_MESSAGE_CODE_INVALID_REQUEST:
      return QObject::tr("The request could not be processed.");
    case onerss::pb::UI_MESSAGE_CODE_AUTHENTICATION_FAILED:
      return QObject::tr("Authentication failed.");
    case onerss::pb::UI_MESSAGE_CODE_SIGNUP_FAILED:
      return QObject::tr("Sign-up or pairing failed.");
    default:
      return QObject::tr("Server message");
  }
}

UiStatusMessage::Severity toSeverity(const onerss::pb::UiMessageSeverity severity) {
  switch (severity) {
    case onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS:
      return UiStatusMessage::Severity::Success;
    case onerss::pb::UI_MESSAGE_SEVERITY_WARNING:
      return UiStatusMessage::Severity::Warning;
    case onerss::pb::UI_MESSAGE_SEVERITY_ERROR:
      return UiStatusMessage::Severity::Error;
    case onerss::pb::UI_MESSAGE_SEVERITY_INFO:
    case onerss::pb::UI_MESSAGE_SEVERITY_UNSPECIFIED:
    default:
      return UiStatusMessage::Severity::Info;
  }
}

}  // namespace

UiStatusMessage toUiStatusMessage(const onerss::pb::UserNotification &notification) {
  return UiStatusMessage{
    .severity = toSeverity(notification.severity()),
    .text = translatedCode(notification.code()),
    .detail = QString::fromStdString(notification.context()),
  };
}

}  // namespace onerss::desktop
