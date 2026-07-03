#include "feed_refresh_scheduler.h"

#include "logging.h"

#include <algorithm>
#include <chrono>

namespace onerss::backend {
namespace {

constexpr std::uint32_t kDefaultRefreshHours = 12;

std::chrono::steady_clock::time_point nextRunFromNow(const std::uint32_t hours) {
  return std::chrono::steady_clock::now() + std::chrono::hours{std::max(1u, hours)};
}

std::chrono::steady_clock::time_point jitteredStartupRun(const std::uint32_t hours, std::mt19937_64 &random_engine) {
  const auto interval_seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::hours{std::max(1u, hours)}).count();
  const auto max_jitter_seconds = std::max<std::int64_t>(1, interval_seconds / 5);
  std::uniform_int_distribution<std::int64_t> distribution{0, max_jitter_seconds};
  return std::chrono::steady_clock::now() + std::chrono::seconds{distribution(random_engine)};
}

}  // namespace

FeedRefreshScheduler::FeedRefreshScheduler(TreeRepository &tree_repository,
                                           UserStore &user_store,
                                           FeedFetcher &feed_fetcher,
                                           const std::size_t num_concurrent_fetches,
                                           ArticlesUpdatedFn on_articles_updated)
    : tree_repository_{tree_repository},
      user_store_{user_store},
      feed_fetcher_{feed_fetcher},
      num_concurrent_fetches_{std::max<std::size_t>(1, num_concurrent_fetches)},
      on_articles_updated_{std::move(on_articles_updated)} {}

void FeedRefreshScheduler::stop() {
  stop_requested_.store(true);
  cv_.notify_all();
  if (scheduler_thread_.joinable()) {
    scheduler_thread_.join();
  }
  for (auto &worker : worker_threads_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  worker_threads_.clear();
}

FeedRefreshScheduler::~FeedRefreshScheduler() {
  stop();
}

void FeedRefreshScheduler::start() {
  std::scoped_lock lock{mutex_};
  if (started_) {
    return;
  }
  started_ = true;
  stop_requested_.store(false);
  scheduler_thread_ = std::thread([this]() { schedulerLoop(); });
  worker_threads_.reserve(num_concurrent_fetches_);
  for (std::size_t i = 0; i < num_concurrent_fetches_; ++i) {
    worker_threads_.emplace_back([this]() { workerLoop(); });
  }
  LOG_INFO << "Started feed refresh scheduler with " << num_concurrent_fetches_ << " concurrent workers";
}

void FeedRefreshScheduler::scheduleExistingFeeds() {
  std::unordered_map<std::string, std::uint32_t> defaults;
  std::unordered_map<std::string, std::size_t> per_user_counts;
  std::size_t total = 0;
  {
    std::scoped_lock lock{mutex_};
    feeds_.clear();
    ready_queue_.clear();
  }

  for (const auto &feed : tree_repository_.listAllFeeds()) {
    auto it = defaults.find(feed.user_id);
    if (it == defaults.end()) {
      it = defaults.emplace(feed.user_id, user_store_.getUserSettings(feed.user_id).default_refresh_interval_hours).first;
    }
    {
      std::scoped_lock lock{mutex_};
      upsertFeedLocked(feed, it->second, false, true);
      const auto &state = feeds_.at(feed.node_id);
      const auto seconds_until = std::chrono::duration_cast<std::chrono::seconds>(state.next_run - std::chrono::steady_clock::now()).count();
      LOG_TRACE << "Startup scheduled feed node=" << feed.node_id
                << " user=" << feed.user_id
                << " title=" << feed.title
                << " url=" << feed.feed_url
                << " next_fetch_in_seconds=" << seconds_until
                << " refresh_hours=" << state.effective_refresh_hours;
    }
    ++per_user_counts[feed.user_id];
    ++total;
  }
  LOG_INFO << "Scheduled " << total << " feed fetches at startup";
  for (const auto &[user_id, count] : per_user_counts) {
    LOG_DEBUG << "Scheduled " << count << " feeds for user=" << user_id;
  }
  {
    std::scoped_lock lock{mutex_};
    schedule_changed_ = true;
  }
  cv_.notify_all();
}

void FeedRefreshScheduler::scheduleNow(const TreeNodeRecord &feed) {
  if (feed.type != onerss::pb::TreeNode::TYPE_FEED) {
    return;
  }
  const auto user_settings = user_store_.getUserSettings(feed.user_id);
  {
    std::scoped_lock lock{mutex_};
    upsertFeedLocked(feed, user_settings.default_refresh_interval_hours, true, false);
    schedule_changed_ = true;
  }
  LOG_DEBUG << "Scheduled immediate fetch for feed node=" << feed.node_id;
  cv_.notify_all();
}

void FeedRefreshScheduler::reloadUserFeeds(const std::string &user_id) {
  const auto user_settings = user_store_.getUserSettings(user_id);
  const auto nodes = tree_repository_.listNodesForUser(user_id);
  {
    std::scoped_lock lock{mutex_};
    for (auto it = feeds_.begin(); it != feeds_.end();) {
      if (it->second.node.user_id == user_id) {
        it = feeds_.erase(it);
      } else {
        ++it;
      }
    }
    ready_queue_.erase(std::remove_if(ready_queue_.begin(),
                                      ready_queue_.end(),
                                      [this, &user_id](const auto &entry) {
                                        const auto it = feeds_.find(entry.first);
                                        return it == feeds_.end() || it->second.node.user_id == user_id;
                                      }),
                       ready_queue_.end());
    for (const auto &node : nodes) {
      if (node.type == onerss::pb::TreeNode::TYPE_FEED) {
        upsertFeedLocked(node, user_settings.default_refresh_interval_hours, false, true);
      }
    }
    schedule_changed_ = true;
  }
  LOG_DEBUG << "Reloaded scheduled feeds for user=" << user_id;
  cv_.notify_all();
}

void FeedRefreshScheduler::removeNode(const std::string &node_id) {
  {
    std::scoped_lock lock{mutex_};
    feeds_.erase(node_id);
    ready_queue_.erase(std::remove_if(ready_queue_.begin(),
                                      ready_queue_.end(),
                                      [&node_id](const auto &entry) { return entry.first == node_id; }),
                       ready_queue_.end());
    schedule_changed_ = true;
  }
  cv_.notify_all();
}

void FeedRefreshScheduler::schedulerLoop() {
  std::unique_lock lock{mutex_};
  while (!stop_requested_.load()) {
    const auto now = std::chrono::steady_clock::now();
    enqueueDueFeedsLocked(now);
    schedule_changed_ = false;
    if (!ready_queue_.empty()) {
      cv_.notify_all();
    }

    auto next_due = std::chrono::steady_clock::time_point::max();
    for (const auto &[_, state] : feeds_) {
      if (!state.queued && !state.running) {
        next_due = std::min(next_due, state.next_run);
      }
    }
    if (!ready_queue_.empty()) {
      cv_.wait(lock, [this]() { return stop_requested_.load() || schedule_changed_ || ready_queue_.empty(); });
    } else if (next_due == std::chrono::steady_clock::time_point::max()) {
      cv_.wait(lock, [this]() { return stop_requested_.load() || schedule_changed_ || !ready_queue_.empty(); });
    } else {
      cv_.wait_until(lock,
                     next_due,
                     [this]() { return stop_requested_.load() || schedule_changed_; });
    }
  }
}

void FeedRefreshScheduler::workerLoop() {
  while (!stop_requested_.load()) {
    std::pair<std::string, std::uint64_t> task;
    {
      std::unique_lock lock{mutex_};
      cv_.wait(lock, [this]() { return stop_requested_.load() || !ready_queue_.empty(); });
      if (stop_requested_.load()) {
        return;
      }
      task = ready_queue_.front();
      ready_queue_.pop_front();

      auto it = feeds_.find(task.first);
      if (it == feeds_.end() || it->second.generation != task.second) {
        LOG_TRACE << "Discarded queued scheduled fetch for feed node=" << task.first << " due to stale generation";
        cv_.notify_all();
        continue;
      }
      it->second.queued = false;
      it->second.running = true;
      LOG_TRACE << "Dispatching scheduled fetch for feed node=" << task.first;
      cv_.notify_all();
    }

    executeFetch(task.first, task.second);
  }
}

void FeedRefreshScheduler::enqueueDueFeedsLocked(const std::chrono::steady_clock::time_point now) {
  for (auto &[node_id, state] : feeds_) {
    if (!state.queued && !state.running && state.next_run <= now) {
      state.queued = true;
      ready_queue_.emplace_back(node_id, state.generation);
      LOG_TRACE << "Queued scheduled fetch for feed node=" << node_id;
    }
  }
}

void FeedRefreshScheduler::upsertFeedLocked(const TreeNodeRecord &feed,
                                            const std::uint32_t default_refresh_hours,
                                            const bool immediate,
                                            const bool startup_staggered) {
  auto &state = feeds_[feed.node_id];
  state.node = feed;
  state.effective_refresh_hours
    = std::max(1u, feed.use_default_refresh_interval ? default_refresh_hours : feed.refresh_interval_hours);
  state.generation += 1;
  if (immediate) {
    state.next_run = std::chrono::steady_clock::now();
  } else if (startup_staggered) {
    state.next_run = jitteredStartupRun(state.effective_refresh_hours, random_engine_);
  } else if (!state.running && !state.queued) {
    state.next_run = nextRunFromNow(state.effective_refresh_hours);
  }
}

void FeedRefreshScheduler::executeFetch(const std::string &node_id, const std::uint64_t generation) {
  TreeNodeRecord feed;
  std::uint32_t refresh_hours = kDefaultRefreshHours;
  {
    std::scoped_lock lock{mutex_};
    const auto it = feeds_.find(node_id);
    if (it == feeds_.end() || it->second.generation != generation) {
      return;
    }
    feed = it->second.node;
    refresh_hours = it->second.effective_refresh_hours;
  }

  try {
    const auto started_at = std::chrono::steady_clock::now();
    LOG_TRACE << "Fetch starting node=" << feed.node_id
              << " title=" << feed.title
              << " url=" << feed.feed_url;
    const auto articles = feed_fetcher_.fetchArticles(feed);
    const auto new_entries = tree_repository_.upsertArticles(feed.user_id, feed.node_id, articles);
    if (on_articles_updated_) {
      on_articles_updated_(feed.user_id, feed.node_id);
    }
    const auto duration_ms
      = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
    LOG_TRACE << "Fetch finished node=" << feed.node_id
              << " title=" << feed.title
              << " url=" << feed.feed_url
              << " duration_ms=" << duration_ms
              << " entries=" << articles.size()
              << " new_entries=" << new_entries;
  } catch (const std::exception &error) {
    LOG_WARN << "Scheduled refresh failed for feed node=" << feed.node_id << ": " << error.what();
  }

  {
    std::scoped_lock lock{mutex_};
    const auto it = feeds_.find(node_id);
    if (it == feeds_.end()) {
      return;
    }
    it->second.running = false;
    if (it->second.generation == generation) {
      it->second.next_run = nextRunFromNow(refresh_hours);
    }
    schedule_changed_ = true;
  }
  cv_.notify_all();
}

}  // namespace onerss::backend
