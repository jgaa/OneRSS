#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>

namespace onerss::desktop {

template <typename Stream, typename Envelope>
Envelope readEnvelope(Stream &stream);

template <typename Stream, typename Envelope>
void writeEnvelope(Stream &stream, const Envelope &envelope);

template <typename Envelope>
class OutgoingMessageQueue final {
 public:
  void push(Envelope envelope) {
    std::scoped_lock lock{mutex_};
    queue_.push(std::move(envelope));
    condition_.notify_one();
  }

  bool waitPop(Envelope &envelope) {
    std::unique_lock lock{mutex_};
    condition_.wait(lock, [this]() { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    envelope = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  template <typename Rep, typename Period>
  bool waitPopFor(Envelope &envelope, const std::chrono::duration<Rep, Period> &timeout) {
    std::unique_lock lock{mutex_};
    condition_.wait_for(lock, timeout, [this]() { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    envelope = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void close() {
    std::scoped_lock lock{mutex_};
    closed_ = true;
    condition_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<Envelope> queue_;
  bool closed_ = false;
};

}  // namespace onerss::desktop

#include "protocol_io_impl.h"
