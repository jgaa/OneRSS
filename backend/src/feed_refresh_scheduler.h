#pragma once

#include "feed_fetcher.h"
#include "tree_repository.h"
#include "user_store.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace onerss::backend {

class FeedRefreshScheduler final {
 public:
  using ArticlesUpdatedFn = std::function<void(const std::string &user_id, const std::string &node_id)>;

  FeedRefreshScheduler(TreeRepository &tree_repository,
                       UserStore &user_store,
                       FeedFetcher &feed_fetcher,
                       std::size_t num_concurrent_fetches,
                       ArticlesUpdatedFn on_articles_updated);
  ~FeedRefreshScheduler();

  void start();
  void scheduleExistingFeeds();
  void scheduleNow(const TreeNodeRecord &feed);
  void reloadUserFeeds(const std::string &user_id);
  void removeNode(const std::string &node_id);

 private:
  struct FeedState {
    TreeNodeRecord node;
    std::uint32_t effective_refresh_hours = 12;
    std::chrono::steady_clock::time_point next_run = std::chrono::steady_clock::now();
    std::uint64_t generation = 0;
    bool queued = false;
    bool running = false;
  };

  void stop();
  void schedulerLoop();
  void workerLoop();
  void enqueueDueFeedsLocked(std::chrono::steady_clock::time_point now);
  void upsertFeedLocked(const TreeNodeRecord &feed,
                        std::uint32_t default_refresh_hours,
                        bool immediate,
                        bool startup_staggered);
  void executeFetch(const std::string &node_id, std::uint64_t generation);

  TreeRepository &tree_repository_;
  UserStore &user_store_;
  FeedFetcher &feed_fetcher_;
  std::size_t num_concurrent_fetches_;
  ArticlesUpdatedFn on_articles_updated_;
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::unordered_map<std::string, FeedState> feeds_;
  std::deque<std::pair<std::string, std::uint64_t>> ready_queue_;
  std::mt19937_64 random_engine_{std::random_device{}()};
  std::thread scheduler_thread_;
  std::vector<std::thread> worker_threads_;
  std::atomic<bool> stop_requested_ = false;
  bool schedule_changed_ = false;
  bool started_ = false;
};

}  // namespace onerss::backend
