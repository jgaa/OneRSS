#include "app_client.h"

#include "logging.h"
#include "protocol_io.h"

#include <algorithm>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace onerss::desktop {
namespace {

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

QString toQString(const std::string &value) {
  return QString::fromStdString(value);
}

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
    = static_cast<int>(settings.default_refresh_interval_hours() == 0 ? 12 : settings.default_refresh_interval_hours()),
  };
}

onerss::pb::UserSettings toProto(const UserSettingsData &settings) {
  onerss::pb::UserSettings proto;
  proto.set_default_refresh_interval_hours(static_cast<std::uint32_t>(std::max(1, settings.default_refresh_interval_hours)));
  return proto;
}

}  // namespace

AppClient::AppClient() : context_{boost::asio::ssl::context::tls_client} {}

AppClient::~AppClient() {
  stop();
}

void AppClient::connectAndStart(const StoredPeer &peer) {
  stop();
  peer_ = peer;

  context_ = ssl_context_t{boost::asio::ssl::context::tls_client};
  context_.set_verify_mode(ssl::verify_peer);
  const auto ca = peer.ca_certificate_pem.toUtf8();
  context_.add_certificate_authority(
    boost::asio::buffer(ca.constData(), static_cast<std::size_t>(ca.size())));
  const auto cert = peer.client_certificate_pem.toUtf8();
  const auto key = peer.client_private_key_pem.toUtf8();
  context_.use_certificate_chain(boost::asio::buffer(cert.constData(), static_cast<std::size_t>(cert.size())));
  context_.use_private_key(boost::asio::buffer(key.constData(), static_cast<std::size_t>(key.size())),
                           ssl::context::pem);

  stream_ = std::make_unique<stream_t>(io_context_, context_);
  tcp::resolver resolver{io_context_};
  const auto endpoints = resolver.resolve(peer.server_host.toStdString(), std::to_string(peer.app_port));
  boost::asio::connect(stream_->next_layer(), endpoints);
  stream_->handshake(ssl::stream_base::client);
  running_ = true;

  reader_thread_ = std::thread([this]() { readerLoop(); });
  writer_thread_ = std::thread([this]() { writerLoop(); });

  onerss::pb::IncomingEnvelope hello;
  hello.set_request_id(newRequestId());
  hello.mutable_app_hello_request();
  const auto response = request(hello);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  LOG_INFO << "Authenticated app client as device_id=" << peer_.device_id.toStdString();
}

void AppClient::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  outgoing_.close();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  if (writer_thread_.joinable()) {
    writer_thread_.join();
  }
  stream_.reset();
}

QVector<TreeNodeData> AppClient::fetchTree() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_fetch_tree_request();
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }

  QVector<TreeNodeData> nodes;
  for (const auto &node : response.tree_nodes_response().nodes()) {
    nodes.push_back(fromProto(node));
  }
  return nodes;
}

UserSettingsData AppClient::fetchUserSettings() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_get_user_settings_request();
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return fromProto(response.user_settings_response().settings());
}

UserSettingsData AppClient::updateUserSettings(const UserSettingsData &settings) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  *request_envelope.mutable_update_user_settings_request()->mutable_settings() = toProto(settings);
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return fromProto(response.user_settings_response().settings());
}

int AppClient::fetchUnreadCount() {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_get_unread_count_request();
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return static_cast<int>(response.unread_count_response().unread_count());
}

ArticlePage AppClient::fetchArticles(const QString &node_id, const int offset, const int limit) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  auto *payload = request_envelope.mutable_fetch_articles_request();
  payload->set_node_id(node_id == QStringLiteral("__root__") ? std::string{} : node_id.toStdString());
  payload->set_offset(static_cast<std::uint32_t>(std::max(0, offset)));
  payload->set_limit(static_cast<std::uint32_t>(std::max(0, limit)));
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }

  ArticlePage page;
  for (const auto &article : response.articles_response().articles()) {
    page.articles.push_back(fromProto(article));
  }
  page.has_more = response.articles_response().has_more();
  page.next_offset = static_cast<int>(response.articles_response().next_offset());
  return page;
}

int AppClient::markArticleRead(const QString &node_id, const QString &article_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  auto *payload = request_envelope.mutable_mark_article_read_request();
  payload->set_node_id(node_id.toStdString());
  payload->set_article_id(article_id.toStdString());
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return static_cast<int>(response.mark_articles_read_response().changed_count());
}

int AppClient::markAllArticlesRead(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_mark_all_articles_read_request()->set_node_id(node_id.toStdString());
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return static_cast<int>(response.mark_articles_read_response().changed_count());
}

TreeNodeData AppClient::createFolder(const QString &parent_id, const QString &title) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  auto *payload = request_envelope.mutable_create_folder_request();
  payload->set_parent_id(parent_id.toStdString());
  payload->set_title(title.toStdString());
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return fromProto(response.tree_node_response().node());
}

TreeNodeData AppClient::createFeed(const QString &parent_id,
                                   const QString &title,
                                   const QString &feed_url,
                                   const QString &comment) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  auto *payload = request_envelope.mutable_create_feed_request();
  payload->set_parent_id(parent_id.toStdString());
  payload->set_title(title.toStdString());
  payload->set_feed_url(feed_url.toStdString());
  payload->set_comment(comment.toStdString());
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return fromProto(response.tree_node_response().node());
}

TreeNodeData AppClient::updateNode(const TreeNodeData &node) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  *request_envelope.mutable_update_node_request()->mutable_node() = toProto(node);
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
  return fromProto(response.tree_node_response().node());
}

void AppClient::deleteNode(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_delete_node_request()->set_node_id(node_id.toStdString());
  const auto response = request(request_envelope);
  emitUserNotification(response);
  if (response.has_error()) {
    throw BackendCommandError{response.error().message(), toUiStatusMessage(response.error().user_notification())};
  }
}

void AppClient::refreshFeed(const QString &node_id) {
  onerss::pb::IncomingEnvelope request_envelope;
  request_envelope.set_request_id(newRequestId());
  request_envelope.mutable_refresh_feed_request()->set_node_id(node_id.toStdString());
  const auto response = request(request_envelope);
  if (response.has_error()) {
    throw std::runtime_error{response.error().message()};
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
    pending_.emplace(request_envelope.request_id(), std::move(promise));
  }
  outgoing_.push(request_envelope);
  return future.get();
}

void AppClient::emitUserNotification(const onerss::pb::OutgoingEnvelope &envelope) {
  if (envelope.has_user_notification() && onUserNotification) {
    onUserNotification(toUiStatusMessage(envelope.user_notification()));
  }
}

void AppClient::readerLoop() {
  try {
    while (running_ && stream_ != nullptr) {
      auto envelope = readEnvelope<stream_t, onerss::pb::OutgoingEnvelope>(*stream_);
      emitUserNotification(envelope);
      if (envelope.request_id().empty()) {
        if (envelope.has_tree_node_upserted_notification() && onNodeUpserted) {
          const auto &payload = envelope.tree_node_upserted_notification();
          LOG_TRACE << "Received tree node upserted notification node_id=" << payload.node().node_id()
                    << " origin_device_id=" << payload.origin_device_id();
          onNodeUpserted(fromProto(payload.node()), toQString(payload.origin_device_id()));
        } else if (envelope.has_tree_node_deleted_notification() && onNodeDeleted) {
          const auto &payload = envelope.tree_node_deleted_notification();
          LOG_TRACE << "Received tree node deleted notification node_id=" << payload.node_id()
                    << " origin_device_id=" << payload.origin_device_id();
          onNodeDeleted(toQString(payload.node_id()), toQString(payload.origin_device_id()));
        } else if (envelope.has_articles_updated_notification() && onArticlesUpdated) {
          const auto &payload = envelope.articles_updated_notification();
          LOG_TRACE << "Received articles updated notification node_id=" << payload.node_id()
                    << " origin_device_id=" << payload.origin_device_id();
          onArticlesUpdated(toQString(payload.node_id()), toQString(payload.origin_device_id()));
        } else if (envelope.has_user_settings_updated_notification() && onUserSettingsUpdated) {
          const auto &payload = envelope.user_settings_updated_notification();
          LOG_TRACE << "Received user settings updated notification origin_device_id="
                    << payload.origin_device_id()
                    << " default_refresh_interval_hours="
                    << payload.settings().default_refresh_interval_hours();
          onUserSettingsUpdated(fromProto(payload.settings()), toQString(payload.origin_device_id()));
        } else {
          LOG_TRACE << "Received unhandled notification envelope";
        }
        continue;
      }

      std::scoped_lock lock{pending_mutex_};
      auto it = pending_.find(envelope.request_id());
      if (it != pending_.end()) {
        it->second.set_value(std::move(envelope));
        pending_.erase(it);
      }
    }
  } catch (const std::exception &error) {
    LOG_WARN << "App client reader stopped: " << error.what();
  }
}

void AppClient::writerLoop() {
  try {
    onerss::pb::IncomingEnvelope envelope;
    while (outgoing_.waitPop(envelope)) {
      if (stream_ == nullptr) {
        break;
      }
      writeEnvelope<stream_t, onerss::pb::IncomingEnvelope>(*stream_, envelope);
    }
  } catch (const std::exception &error) {
    LOG_WARN << "App client writer stopped: " << error.what();
  }
}

std::string AppClient::newRequestId() {
  return boost::uuids::to_string(boost::uuids::random_generator()());
}

}  // namespace onerss::desktop
