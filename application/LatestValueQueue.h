#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace omnidetect {

template <typename T>
class LatestValueQueue {
 public:
  struct PushResult {
    bool accepted{false};
    std::size_t dropped{0};
  };

  explicit LatestValueQueue(std::size_t capacity, bool dropOld = true)
      : capacity_(capacity == 0 ? 1 : capacity), dropOld_(dropOld) {}

  LatestValueQueue(const LatestValueQueue&) = delete;
  LatestValueQueue& operator=(const LatestValueQueue&) = delete;

  PushResult push(T value) {
    std::unique_lock lock(mutex_);
    if (!dropOld_) spaceAvailable_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
    if (closed_) return {};
    std::size_t dropped = 0;
    while (queue_.size() >= capacity_) {
      queue_.pop_front();
      ++dropped;
    }
    queue_.push_back(std::move(value));
    valueAvailable_.notify_one();
    return {true, dropped};
  }

  [[nodiscard]] std::optional<T> pop() {
    std::unique_lock lock(mutex_);
    valueAvailable_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    spaceAvailable_.notify_one();
    return value;
  }

  // Removes and returns the newest value without blocking. Older queued values are discarded.
  [[nodiscard]] std::optional<T> tryPopLatest(std::size_t* discarded = nullptr) {
    const std::scoped_lock lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    const auto older = queue_.size() - 1U;
    T value = std::move(queue_.back());
    queue_.clear();
    if (discarded != nullptr) *discarded = older;
    spaceAvailable_.notify_all();
    return value;
  }

  void clear() noexcept {
    const std::scoped_lock lock(mutex_);
    queue_.clear();
    spaceAvailable_.notify_all();
  }

  void close() noexcept {
    const std::scoped_lock lock(mutex_);
    closed_ = true;
    valueAvailable_.notify_all();
    spaceAvailable_.notify_all();
  }

  void reset() noexcept {
    const std::scoped_lock lock(mutex_);
    queue_.clear();
    closed_ = false;
    valueAvailable_.notify_all();
    spaceAvailable_.notify_all();
  }

  [[nodiscard]] std::size_t size() const noexcept {
    const std::scoped_lock lock(mutex_);
    return queue_.size();
  }

  [[nodiscard]] bool closed() const noexcept {
    const std::scoped_lock lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

 private:
  const std::size_t capacity_;
  const bool dropOld_;
  mutable std::mutex mutex_;
  std::condition_variable valueAvailable_;
  std::condition_variable spaceAvailable_;
  std::deque<T> queue_;
  bool closed_{false};
};

}  // namespace omnidetect
