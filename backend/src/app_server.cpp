#include "app_server.h"

#include "logging.h"
#include "protocol_io.h"
#include "user_notification.h"

#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace onerss::backend {
namespace {

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

constexpr std::size_t kAllFeedsMaxArticles = 1000;
constexpr std::size_t kDefaultFeedPageSize = 200;

onerss::pb::UserSettings toProto(const UserSettingsRecord &record) {
  onerss::pb::UserSettings settings;
  settings.set_default_refresh_interval_hours(record.default_refresh_interval_hours);
  return settings;
}

UserSettingsRecord fromProto(const onerss::pb::UserSettings &settings) {
  return UserSettingsRecord{
    .default_refresh_interval_hours
    = settings.default_refresh_interval_hours() == 0 ? 12 : settings.default_refresh_interval_hours(),
  };
}

ssl::context createSslContext(const StoredAuthority &authority) {
  ssl::context context{ssl::context::tls_server};
  context.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2
                      | ssl::context::no_sslv3 | ssl::context::single_dh_use);
  context.use_certificate_chain(
    boost::asio::buffer(authority.server_certificate_pem.data(), authority.server_certificate_pem.size()));
  context.use_private_key(
    boost::asio::buffer(authority.server_private_key_pem.data(), authority.server_private_key_pem.size()),
    ssl::context::pem);
  context.add_certificate_authority(
    boost::asio::buffer(authority.ca_certificate_pem.data(), authority.ca_certificate_pem.size()));
  context.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
  return context;
}
}  // namespace

class AppSession final : public std::enable_shared_from_this<AppSession> {
 public:
  AppSession(AppServer &server,
             UserStore &user_store,
             TreeRepository &tree_repository,
             FeedFetcher &feed_fetcher,
             tcp::socket socket)
      : server_{server},
        user_store_{user_store},
        tree_repository_{tree_repository},
        feed_fetcher_{feed_fetcher},
        socket_{std::move(socket)} {}

  ~AppSession() {
    stop();
  }

  void start() {
    auto self = shared_from_this();
    reader_thread_ = std::thread([self]() { self->run(); });
    reader_thread_.detach();
  }

  void enqueue(onerss::pb::OutgoingEnvelope envelope) {
    outgoing_.push(std::move(envelope));
  }

  [[nodiscard]] const std::string &userId() const noexcept {
    return user_id_;
  }

 private:
  void run() {
    try {
      auto context = createSslContext(user_store_.authority());
      ssl::stream<tcp::socket> stream{std::move(socket_), context};
      stream_ = &stream;
      stream.handshake(ssl::stream_base::server);
      authenticate(stream);
      server_.registerSession(user_id_, shared_from_this());

      writer_thread_ = std::thread([this]() { writeLoop(); });

      for (;;) {
        auto request = readEnvelope<ssl::stream<tcp::socket>, onerss::pb::IncomingEnvelope>(stream);
        handleRequest(request);
      }
    } catch (const std::exception &error) {
      LOG_WARN << "App session ended: " << error.what();
    }

    stop();
  }

  void stop() {
    outgoing_.close();
    if (!user_id_.empty()) {
      server_.unregisterSession(user_id_, this);
      user_id_.clear();
    }
    if (writer_thread_.joinable()) {
      writer_thread_.join();
    }
  }

  void authenticate(ssl::stream<tcp::socket> &stream) {
    X509 *peer = SSL_get_peer_certificate(stream.native_handle());
    if (peer == nullptr) {
      throw std::runtime_error{"missing client certificate"};
    }

    std::unique_ptr<X509, decltype(&X509_free)> peer_ptr{peer, &X509_free};
    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
      throw std::runtime_error{"failed to allocate certificate buffer"};
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio_ptr{bio, &BIO_free};
    if (PEM_write_bio_X509(bio_ptr.get(), peer_ptr.get()) != 1) {
      throw std::runtime_error{"failed to serialize peer certificate"};
    }

    BUF_MEM *buffer = nullptr;
    BIO_get_mem_ptr(bio_ptr.get(), &buffer);
    const std::string certificate_pem = buffer != nullptr ? std::string{buffer->data, buffer->length}
                                                          : std::string{};
    const auto device = user_store_.authenticateDeviceCertificate(certificate_pem);
    user_id_ = device.user_id;
    device_id_ = device.device_id;
    login_ = device.login;
    LOG_INFO << "Authenticated app session for user=" << user_id_ << " device=" << device_id_;
  }

  void handleRequest(const onerss::pb::IncomingEnvelope &request) {
    onerss::pb::OutgoingEnvelope response;
    response.set_request_id(request.request_id());

    try {
      if (request.has_app_hello_request()) {
        auto *hello = response.mutable_app_hello_response();
        hello->set_user_id(user_id_);
        hello->set_device_id(device_id_);
        hello->set_login(login_);
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_CONNECTED,
                           "app session ready");
      } else if (request.has_get_user_settings_request()) {
        *response.mutable_user_settings_response()->mutable_settings() = toProto(user_store_.getUserSettings(user_id_));
      } else if (request.has_update_user_settings_request()) {
        const auto settings = user_store_.updateUserSettings(user_id_, fromProto(request.update_user_settings_request().settings()));
        *response.mutable_user_settings_response()->mutable_settings() = toProto(settings);
        server_.reloadScheduledFeedsForUser(user_id_);
        server_.broadcastUserSettingsUpdated(user_id_, device_id_, settings);
      } else if (request.has_fetch_tree_request()) {
        auto *tree = response.mutable_tree_nodes_response();
        for (const auto &node : tree_repository_.listNodesForUser(user_id_)) {
          *tree->add_nodes() = toProto(node);
        }
      } else if (request.has_create_folder_request()) {
        const auto node = tree_repository_.createFolder(user_id_,
                                                        request.create_folder_request().parent_id(),
                                                        request.create_folder_request().title());
        *response.mutable_tree_node_response()->mutable_node() = toProto(node);
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_TREE_NODE_CREATED,
                           node.title);
        server_.broadcastNodeUpserted(user_id_, device_id_, node);
      } else if (request.has_create_feed_request()) {
        const auto node = tree_repository_.createFeed(user_id_,
                                                      request.create_feed_request().parent_id(),
                                                      request.create_feed_request().title(),
                                                      request.create_feed_request().feed_url(),
                                                      request.create_feed_request().comment());
        server_.scheduleFeedNow(node);
        *response.mutable_tree_node_response()->mutable_node() = toProto(node);
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_TREE_NODE_CREATED,
                           node.title);
        server_.broadcastNodeUpserted(user_id_, device_id_, node);
      } else if (request.has_update_node_request()) {
        const auto node = tree_repository_.updateNode(
          user_id_, fromProto(user_id_, request.update_node_request().node()));
        if (node.type == onerss::pb::TreeNode::TYPE_FEED) {
          server_.scheduleFeedNow(node);
        }
        *response.mutable_tree_node_response()->mutable_node() = toProto(node);
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_TREE_NODE_UPDATED,
                           node.title);
        server_.broadcastNodeUpserted(user_id_, device_id_, node);
      } else if (request.has_delete_node_request()) {
        tree_repository_.deleteNode(user_id_, request.delete_node_request().node_id());
        server_.reloadScheduledFeedsForUser(user_id_);
        response.mutable_delete_node_response()->set_node_id(request.delete_node_request().node_id());
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_TREE_NODE_DELETED,
                           request.delete_node_request().node_id());
        server_.broadcastNodeDeleted(user_id_, device_id_, request.delete_node_request().node_id());
      } else if (request.has_refresh_feed_request()) {
        const auto feed = tree_repository_.getNode(user_id_, request.refresh_feed_request().node_id());
        if (feed.type != onerss::pb::TreeNode::TYPE_FEED) {
          throw std::runtime_error{"refresh is only allowed for feed nodes"};
        }
        const auto started_at = std::chrono::steady_clock::now();
        LOG_TRACE << "Fetch starting node=" << feed.node_id
                  << " title=" << feed.title
                  << " url=" << feed.feed_url;
        const auto articles = feed_fetcher_.fetchArticles(feed);
        const auto new_entries = tree_repository_.upsertArticles(user_id_, feed.node_id, articles);
        const auto duration_ms
          = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
        LOG_TRACE << "Fetch finished node=" << feed.node_id
                  << " title=" << feed.title
                  << " url=" << feed.feed_url
                  << " duration_ms=" << duration_ms
                  << " entries=" << articles.size()
                  << " new_entries=" << new_entries;
        auto *payload = response.mutable_refresh_feed_response();
        payload->set_node_id(feed.node_id);
        payload->set_fetched_article_count(static_cast<std::uint32_t>(articles.size()));
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_SUCCESS,
                           onerss::pb::UI_MESSAGE_CODE_FEED_REFRESHED,
                           feed.title);
        server_.broadcastArticlesUpdated(user_id_, device_id_, feed.node_id);
      } else if (request.has_fetch_articles_request()) {
        auto *payload = response.mutable_articles_response();
        const auto node_id = request.fetch_articles_request().node_id();
        const auto requested_offset = static_cast<std::size_t>(request.fetch_articles_request().offset());
        const auto requested_limit = static_cast<std::size_t>(request.fetch_articles_request().limit());
        const bool all_feeds = node_id.empty();

        if (all_feeds && requested_offset >= kAllFeedsMaxArticles) {
          payload->set_has_more(false);
          payload->set_next_offset(static_cast<std::uint32_t>(requested_offset));
        } else {
          const auto page_limit = requested_limit == 0
                                    ? (all_feeds ? kAllFeedsMaxArticles : kDefaultFeedPageSize)
                                    : requested_limit;
          const auto effective_limit = all_feeds
                                         ? std::min(page_limit, kAllFeedsMaxArticles - requested_offset)
                                         : page_limit;
          const auto articles = tree_repository_.listArticles(user_id_, node_id, requested_offset, effective_limit + 1);
          const auto has_more = articles.size() > effective_limit;
          const auto article_count = has_more ? effective_limit : articles.size();
          for (std::size_t i = 0; i < article_count; ++i) {
            *payload->add_articles() = toProto(articles[i]);
          }
          payload->set_has_more(has_more);
          payload->set_next_offset(static_cast<std::uint32_t>(requested_offset + article_count));
        }
        attachNotification(response,
                           onerss::pb::UI_MESSAGE_SEVERITY_INFO,
                           onerss::pb::UI_MESSAGE_CODE_ARTICLES_LOADED,
                           node_id.empty() ? "all feeds" : node_id);
      } else if (request.has_get_unread_count_request()) {
        response.mutable_unread_count_response()->set_unread_count(
          static_cast<std::uint32_t>(tree_repository_.unreadCount(user_id_)));
      } else if (request.has_mark_article_read_request()) {
        const auto &payload = request.mark_article_read_request();
        const auto changed = tree_repository_.markArticleRead(user_id_, payload.node_id(), payload.article_id());
        auto *reply = response.mutable_mark_articles_read_response();
        reply->set_node_id(payload.node_id());
        reply->set_changed_count(static_cast<std::uint32_t>(changed));
        if (changed > 0) {
          server_.broadcastArticlesUpdated(user_id_, device_id_, payload.node_id());
        }
      } else if (request.has_mark_all_articles_read_request()) {
        const auto &payload = request.mark_all_articles_read_request();
        const auto changed = tree_repository_.markAllArticlesRead(user_id_, payload.node_id());
        auto *reply = response.mutable_mark_articles_read_response();
        reply->set_node_id(payload.node_id());
        reply->set_changed_count(static_cast<std::uint32_t>(changed));
        if (changed > 0) {
          server_.broadcastArticlesUpdated(user_id_, device_id_, payload.node_id());
        }
      } else {
        throw std::runtime_error{"unsupported app request"};
      }
    } catch (const std::exception &error) {
      auto ui_code = onerss::pb::UI_MESSAGE_CODE_INVALID_REQUEST;
      if (request.has_create_folder_request() || request.has_create_feed_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_TREE_NODE_CREATE_FAILED;
      } else if (request.has_update_node_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_TREE_NODE_UPDATE_FAILED;
      } else if (request.has_delete_node_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_TREE_NODE_DELETE_FAILED;
      } else if (request.has_refresh_feed_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_FEED_REFRESH_FAILED;
      } else if (request.has_fetch_articles_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_ARTICLES_LOAD_FAILED;
      } else if (request.has_mark_article_read_request() || request.has_mark_all_articles_read_request()) {
        ui_code = onerss::pb::UI_MESSAGE_CODE_INVALID_REQUEST;
      }

      attachError(response,
                  "app_request_failed",
                  error.what(),
                  ui_code,
                  onerss::pb::UI_MESSAGE_SEVERITY_ERROR,
                  error.what());
      LOG_WARN << "App request failed for user=" << user_id_ << ": " << error.what();
    }

    enqueue(std::move(response));
  }

  void writeLoop() {
    try {
      onerss::pb::OutgoingEnvelope envelope;
      while (outgoing_.waitPop(envelope)) {
        if (stream_ == nullptr) {
          break;
        }
        writeEnvelope<ssl::stream<tcp::socket>, onerss::pb::OutgoingEnvelope>(*stream_, envelope);
      }
    } catch (const std::exception &error) {
      LOG_WARN << "App session writer failed: " << error.what();
    }
  }

  AppServer &server_;
  UserStore &user_store_;
  TreeRepository &tree_repository_;
  FeedFetcher &feed_fetcher_;
  tcp::socket socket_;
  ssl::stream<tcp::socket> *stream_ = nullptr;
  std::string user_id_;
  std::string device_id_;
  std::string login_;
  OutgoingMessageQueue<onerss::pb::OutgoingEnvelope> outgoing_;
  std::thread reader_thread_;
  std::thread writer_thread_;
};

AppServer::AppServer(UserStore &user_store,
                     TreeRepository &tree_repository,
                     FeedFetcher &feed_fetcher,
                     const std::size_t num_concurrent_fetches,
                     std::string bind_address,
                     const std::uint16_t port)
    : user_store_{user_store},
      tree_repository_{tree_repository},
      feed_fetcher_{feed_fetcher},
      scheduler_{tree_repository_,
                 user_store_,
                 feed_fetcher_,
                 num_concurrent_fetches,
                 [this](const std::string &user_id, const std::string &node_id) {
                   broadcastArticlesUpdated(user_id, std::string{}, node_id);
                 }},
      bind_address_{std::move(bind_address)},
      port_{port} {}

void AppServer::run() {
  scheduler_.start();
  scheduler_.scheduleExistingFeeds();
  boost::asio::io_context io_context;
  tcp::endpoint endpoint{boost::asio::ip::make_address(bind_address_), port_};
  tcp::acceptor acceptor{io_context, endpoint};

  LOG_INFO << "App server listening on " << bind_address_ << ":" << port_;
  for (;;) {
    tcp::socket socket{io_context};
    acceptor.accept(socket);
    LOG_DEBUG << "Accepted app connection from " << socket.remote_endpoint();
    std::make_shared<AppSession>(*this, user_store_, tree_repository_, feed_fetcher_, std::move(socket))->start();
  }
}

void AppServer::registerSession(const std::string &user_id, const std::shared_ptr<AppSession> &session) {
  std::scoped_lock lock{sessions_mutex_};
  sessions_by_user_[user_id].push_back(session);
}

void AppServer::unregisterSession(const std::string &user_id, const AppSession *session) {
  std::scoped_lock lock{sessions_mutex_};
  auto it = sessions_by_user_.find(user_id);
  if (it == sessions_by_user_.end()) {
    return;
  }

  auto &sessions = it->second;
  sessions.erase(std::remove_if(sessions.begin(),
                                sessions.end(),
                                [session](const auto &entry) {
                                  const auto locked = entry.lock();
                                  return !locked || locked.get() == session;
                                }),
                 sessions.end());
  if (sessions.empty()) {
    sessions_by_user_.erase(it);
  }
}

void AppServer::broadcastNodeUpserted(const std::string &user_id,
                                      const std::string &origin_device_id,
                                      const TreeNodeRecord &node) {
  std::vector<std::shared_ptr<AppSession>> targets;
  {
    std::scoped_lock lock{sessions_mutex_};
    auto it = sessions_by_user_.find(user_id);
    if (it == sessions_by_user_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      if (auto locked = entry.lock()) {
        targets.push_back(std::move(locked));
      }
    }
  }

  for (auto &target : targets) {
    onerss::pb::OutgoingEnvelope envelope;
    auto *payload = envelope.mutable_tree_node_upserted_notification();
    payload->set_origin_device_id(origin_device_id);
    *payload->mutable_node() = toProto(node);
    target->enqueue(std::move(envelope));
  }
}

void AppServer::broadcastNodeDeleted(const std::string &user_id,
                                     const std::string &origin_device_id,
                                     const std::string &node_id) {
  std::vector<std::shared_ptr<AppSession>> targets;
  {
    std::scoped_lock lock{sessions_mutex_};
    auto it = sessions_by_user_.find(user_id);
    if (it == sessions_by_user_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      if (auto locked = entry.lock()) {
        targets.push_back(std::move(locked));
      }
    }
  }

  for (auto &target : targets) {
    onerss::pb::OutgoingEnvelope envelope;
    auto *payload = envelope.mutable_tree_node_deleted_notification();
    payload->set_origin_device_id(origin_device_id);
    payload->set_node_id(node_id);
    target->enqueue(std::move(envelope));
  }
}

void AppServer::broadcastArticlesUpdated(const std::string &user_id,
                                         const std::string &origin_device_id,
                                         const std::string &node_id) {
  std::vector<std::shared_ptr<AppSession>> targets;
  {
    std::scoped_lock lock{sessions_mutex_};
    auto it = sessions_by_user_.find(user_id);
    if (it == sessions_by_user_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      if (auto locked = entry.lock()) {
        targets.push_back(std::move(locked));
      }
    }
  }

  for (auto &target : targets) {
    onerss::pb::OutgoingEnvelope envelope;
    auto *payload = envelope.mutable_articles_updated_notification();
    payload->set_origin_device_id(origin_device_id);
    payload->set_node_id(node_id);
    target->enqueue(std::move(envelope));
  }
}

void AppServer::scheduleFeedNow(const TreeNodeRecord &node) {
  scheduler_.scheduleNow(node);
}

void AppServer::reloadScheduledFeedsForUser(const std::string &user_id) {
  scheduler_.reloadUserFeeds(user_id);
}

void AppServer::broadcastUserSettingsUpdated(const std::string &user_id,
                                             const std::string &origin_device_id,
                                             const UserSettingsRecord &settings) {
  std::vector<std::shared_ptr<AppSession>> targets;
  {
    std::scoped_lock lock{sessions_mutex_};
    auto it = sessions_by_user_.find(user_id);
    if (it == sessions_by_user_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      if (auto locked = entry.lock()) {
        targets.push_back(std::move(locked));
      }
    }
  }

  for (auto &target : targets) {
    onerss::pb::OutgoingEnvelope envelope;
    auto *payload = envelope.mutable_user_settings_updated_notification();
    payload->set_origin_device_id(origin_device_id);
    *payload->mutable_settings() = toProto(settings);
    target->enqueue(std::move(envelope));
  }
}

}  // namespace onerss::backend
