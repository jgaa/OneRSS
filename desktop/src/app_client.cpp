#include "app_client.h"

#include "logging.h"
#include "protocol_io.h"

#include <algorithm>
#include <QByteArray>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QUuid>

namespace onerss::desktop {
namespace {

class BackendCommandError final : public std::runtime_error {
 public:
  BackendCommandError(std::string message, UiStatusMessage ui_message)
      : std::runtime_error(std::move(message)), ui_message_(std::move(ui_message)) {}

  [[nodiscard]] const UiStatusMessage &uiMessage() const noexcept {
    return ui_message_;
  }

 private:
  UiStatusMessage ui_message_;
};

UserSettingsData fromProto(const onerss::pb::UserSettings &settings) {
  return UserSettingsData{
    .default_refresh_interval_hours
    = static_cast<int>(settings.defaultRefreshIntervalHours() == 0 ? 12 : settings.defaultRefreshIntervalHours()),
  };
}

onerss::pb::UserSettings toProto(const UserSettingsData &settings) {
  onerss::pb::UserSettings proto;
  proto.setDefaultRefreshIntervalHours(static_cast<QtProtobuf::uint32>(std::max(1, settings.default_refresh_interval_hours)));
  return proto;
}

}  // namespace

AppClient::AppClient() = default;

AppClient::~AppClient() {
  stop();
}

void AppClient::connectAndStart(const StoredPeer &peer) {
  stop();
  peer_ = peer;
  running_ = true;
  std::promise<void> ready_promise;
  auto ready_future = ready_promise.get_future();
  io_thread_ = std::thread([this, ready_promise = std::move(ready_promise)]() mutable {
    ioLoop(std::move(ready_promise));
  });
  ready_future.get();

  onerss::pb::IncomingEnvelope hello;
  hello.setRequestId(QString::fromStdString(newRequestId()));
  hello.setAppHelloRequest(onerss::pb::AppHelloRequest{});
  const auto response = request(hello);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  LOG_INFO << "Authenticated app client as device_id=" << peer_.device_id.toStdString();
}

void AppClient::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  outgoing_.close();
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
}

QVector<TreeNodeData> AppClient::fetchTree() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  request_envelope.setFetchTreeRequest(onerss::pb::FetchTreeRequest{});
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }

  QVector<TreeNodeData> nodes;
  for (const auto &node : response.treeNodesResponse().nodes()) {
    nodes.push_back(fromProto(node));
  }
  return nodes;
}

UserSettingsData AppClient::fetchUserSettings() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  request_envelope.setGetUserSettingsRequest(onerss::pb::GetUserSettingsRequest{});
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return fromProto(response.userSettingsResponse().settings());
}

UserSettingsData AppClient::updateUserSettings(const UserSettingsData &settings) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::UpdateUserSettingsRequest payload;
  payload.setSettings(toProto(settings));
  request_envelope.setUpdateUserSettingsRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return fromProto(response.userSettingsResponse().settings());
}

int AppClient::fetchUnreadCount() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  request_envelope.setGetUnreadCountRequest(onerss::pb::GetUnreadCountRequest{});
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return static_cast<int>(response.unreadCountResponse().unreadCount());
}

ArticlePage AppClient::fetchArticles(const QString &node_id, const int offset, const int limit) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::FetchArticlesRequest payload;
  payload.setNodeId(node_id == QStringLiteral("__root__") ? QString{} : node_id);
  payload.setOffset(static_cast<QtProtobuf::uint32>(std::max(0, offset)));
  payload.setLimit(static_cast<QtProtobuf::uint32>(std::max(0, limit)));
  request_envelope.setFetchArticlesRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }

  ArticlePage page;
  for (const auto &article : response.articlesResponse().articles()) {
    page.articles.push_back(fromProto(article));
  }
  page.has_more = response.articlesResponse().hasMore();
  page.next_offset = static_cast<int>(response.articlesResponse().nextOffset());
  return page;
}

int AppClient::markArticleRead(const QString &node_id, const QString &article_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::MarkArticleReadRequest payload;
  payload.setNodeId(node_id);
  payload.setArticleId(article_id);
  request_envelope.setMarkArticleReadRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return static_cast<int>(response.markArticlesReadResponse().changedCount());
}

int AppClient::markArticleUnread(const QString &node_id, const QString &article_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::MarkArticleUnreadRequest payload;
  payload.setNodeId(node_id);
  payload.setArticleId(article_id);
  request_envelope.setMarkArticleUnreadRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return static_cast<int>(response.markArticlesReadResponse().changedCount());
}

int AppClient::markAllArticlesRead(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::MarkAllArticlesReadRequest payload;
  payload.setNodeId(node_id);
  request_envelope.setMarkAllArticlesReadRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return static_cast<int>(response.markArticlesReadResponse().changedCount());
}

TreeNodeData AppClient::createFolder(const QString &parent_id, const QString &title) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::CreateFolderRequest payload;
  payload.setParentId(parent_id);
  payload.setTitle(title);
  request_envelope.setCreateFolderRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return fromProto(response.treeNodeResponse().node());
}

TreeNodeData AppClient::createFeed(const QString &parent_id,
                                   const QString &title,
                                   const QString &feed_url,
                                   const QString &comment) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::CreateFeedRequest payload;
  payload.setParentId(parent_id);
  payload.setTitle(title);
  payload.setFeedUrl(feed_url);
  payload.setComment(comment);
  request_envelope.setCreateFeedRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return fromProto(response.treeNodeResponse().node());
}

TreeNodeData AppClient::updateNode(const TreeNodeData &node) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::UpdateNodeRequest payload;
  payload.setNode(toProto(node));
  request_envelope.setUpdateNodeRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
  return fromProto(response.treeNodeResponse().node());
}

void AppClient::deleteNode(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::DeleteNodeRequest payload;
  payload.setNodeId(node_id);
  request_envelope.setDeleteNodeRequest(std::move(payload));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.hasError()) {
    throw BackendCommandError{response.error().message().toStdString(),
                              toUiStatusMessage(response.error().userNotification())};
  }
}

void AppClient::refreshFeed(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.setRequestId(QString::fromStdString(newRequestId()));
  onerss::pb::RefreshFeedRequest payload;
  payload.setNodeId(node_id);
  request_envelope.setRefreshFeedRequest(std::move(payload));
  const auto response = request(request_envelope);
  if (response.hasError()) {
    throw std::runtime_error{response.error().message().toStdString()};
  }
}

QString AppClient::deviceId() const {
  return peer_.device_id;
}

onerss::pb::OutgoingEnvelope AppClient::request(const onerss::pb::IncomingEnvelope &request_envelope) {
  std::promise<onerss::pb::OutgoingEnvelope> promise;
  auto future = promise.get_future();
  {
    std::scoped_lock lock{pending_mutex_};
    pending_.emplace(request_envelope.requestId().toStdString(), std::move(promise));
  }
  outgoing_.push(request_envelope);
  return future.get();
}

void AppClient::emitUserNotification(const onerss::pb::OutgoingEnvelope &envelope) {
  if (envelope.hasUserNotification() && onUserNotification) {
    onUserNotification(toUiStatusMessage(envelope.userNotification()));
  }
}

void AppClient::handleEnvelope(onerss::pb::OutgoingEnvelope envelope) {
  emitUserNotification(envelope);
  if (envelope.requestId().isEmpty()) {
    if (envelope.hasTreeNodeUpsertedNotification() && onNodeUpserted) {
      const auto &payload = envelope.treeNodeUpsertedNotification();
      LOG_TRACE << "Received tree node upserted notification node_id=" << payload.node().nodeId().toStdString()
                << " origin_device_id=" << payload.originDeviceId().toStdString();
      onNodeUpserted(fromProto(payload.node()), payload.originDeviceId());
    } else if (envelope.hasTreeNodeDeletedNotification() && onNodeDeleted) {
      const auto &payload = envelope.treeNodeDeletedNotification();
      LOG_TRACE << "Received tree node deleted notification node_id=" << payload.nodeId().toStdString()
                << " origin_device_id=" << payload.originDeviceId().toStdString();
      onNodeDeleted(payload.nodeId(), payload.originDeviceId());
    } else if (envelope.hasArticlesUpdatedNotification() && onArticlesUpdated) {
      const auto &payload = envelope.articlesUpdatedNotification();
      LOG_TRACE << "Received articles updated notification node_id=" << payload.nodeId().toStdString()
                << " origin_device_id=" << payload.originDeviceId().toStdString();
      onArticlesUpdated(payload.nodeId(), payload.originDeviceId());
    } else if (envelope.hasUserSettingsUpdatedNotification() && onUserSettingsUpdated) {
      const auto &payload = envelope.userSettingsUpdatedNotification();
      LOG_TRACE << "Received user settings updated notification origin_device_id="
                << payload.originDeviceId().toStdString()
                << " default_refresh_interval_hours="
                << payload.settings().defaultRefreshIntervalHours();
      onUserSettingsUpdated(fromProto(payload.settings()), payload.originDeviceId());
    } else {
      LOG_TRACE << "Received unhandled notification envelope";
    }
    return;
  }

  std::scoped_lock lock{pending_mutex_};
  auto it = pending_.find(envelope.requestId().toStdString());
  if (it != pending_.end()) {
    it->second.set_value(std::move(envelope));
    pending_.erase(it);
  }
}

void AppClient::ioLoop(std::promise<void> ready_promise) {
  try {
    QSslSocket socket;
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    configuration.setProtocol(QSsl::TlsV1_2OrLater);

    const auto ca_certificates = QSslCertificate::fromData(peer_.ca_certificate_pem.toUtf8(), QSsl::Pem);
    if (ca_certificates.isEmpty()) {
      throw std::runtime_error{"failed to parse CA certificate"};
    }
    configuration.setCaCertificates(ca_certificates);

    const auto client_chain = QSslCertificate::fromData(peer_.client_certificate_pem.toUtf8(), QSsl::Pem);
    if (client_chain.isEmpty()) {
      throw std::runtime_error{"failed to parse client certificate"};
    }
    configuration.setLocalCertificateChain(client_chain);
    configuration.setLocalCertificate(client_chain.front());

    QSslKey private_key{peer_.client_private_key_pem.toUtf8(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey};
    if (private_key.isNull()) {
      private_key = QSslKey{peer_.client_private_key_pem.toUtf8(), QSsl::Ec, QSsl::Pem, QSsl::PrivateKey};
    }
    if (private_key.isNull()) {
      throw std::runtime_error{"failed to parse client private key"};
    }
    configuration.setPrivateKey(private_key);

    socket.setSslConfiguration(configuration);
    socket.connectToHostEncrypted(peer_.server_host,
                                  static_cast<quint16>(peer_.app_port),
                                  QStringLiteral("OneRSS Signup Server"));
    if (!socket.waitForEncrypted(30000)) {
      throw std::runtime_error(socket.errorString().toStdString());
    }
    ready_promise.set_value();

    while (running_) {
      while (socket.bytesAvailable() > 0) {
        handleEnvelope(readEnvelope<QSslSocket, onerss::pb::OutgoingEnvelope>(socket));
      }

      onerss::pb::IncomingEnvelope outgoing_envelope;
      if (outgoing_.waitPopFor(outgoing_envelope, std::chrono::milliseconds{50})) {
        writeEnvelope<QSslSocket, onerss::pb::IncomingEnvelope>(socket, outgoing_envelope);
        continue;
      }

      if (socket.waitForReadyRead(50)) {
        do {
          handleEnvelope(readEnvelope<QSslSocket, onerss::pb::OutgoingEnvelope>(socket));
        } while (socket.bytesAvailable() > 0);
      }
    }
  } catch (const std::exception &error) {
    try {
      ready_promise.set_exception(std::make_exception_ptr(std::runtime_error{error.what()}));
    } catch (...) {
    }
    LOG_WARN << "App client IO stopped: " << error.what();
  }
}

std::string AppClient::newRequestId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

}  // namespace onerss::desktop
