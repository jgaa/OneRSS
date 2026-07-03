#pragma once

#include "feed_refresh_scheduler.h"
#include "feed_fetcher.h"
#include "tree_repository.h"
#include "user_store.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace onerss::backend {

class AppSession;

class AppServer final {
 public:
  AppServer(UserStore &user_store,
            TreeRepository &tree_repository,
            FeedFetcher &feed_fetcher,
            std::size_t num_concurrent_fetches,
            std::string bind_address,
            std::uint16_t port);

  void run();
  void registerSession(const std::string &user_id, const std::shared_ptr<AppSession> &session);
  void unregisterSession(const std::string &user_id, const AppSession *session);
  void broadcastNodeUpserted(const std::string &user_id,
                             const std::string &origin_device_id,
                             const TreeNodeRecord &node);
  void broadcastNodeDeleted(const std::string &user_id,
                            const std::string &origin_device_id,
                            const std::string &node_id);
  void broadcastArticlesUpdated(const std::string &user_id,
                                const std::string &origin_device_id,
                                const std::string &node_id);
  void broadcastUserSettingsUpdated(const std::string &user_id,
                                    const std::string &origin_device_id,
                                    const UserSettingsRecord &settings);
  void scheduleFeedNow(const TreeNodeRecord &node);
  void reloadScheduledFeedsForUser(const std::string &user_id);

 private:
  using tcp = boost::asio::ip::tcp;

  UserStore &user_store_;
  TreeRepository &tree_repository_;
  FeedFetcher &feed_fetcher_;
  FeedRefreshScheduler scheduler_;
  std::string bind_address_;
  std::uint16_t port_;
  std::mutex sessions_mutex_;
  std::unordered_map<std::string, std::vector<std::weak_ptr<AppSession>>> sessions_by_user_;
};

}  // namespace onerss::backend
