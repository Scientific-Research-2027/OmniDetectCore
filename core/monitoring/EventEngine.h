#pragma once

#include "core/monitoring/Event.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace omnidetect {

class EventEngine {
 public:
  using Subscriber = std::function<void(const Event&)>;
  using SubscriptionId = std::uint64_t;

  SubscriptionId subscribe(Subscriber subscriber);
  bool unsubscribe(SubscriptionId id) noexcept;
  void publish(const Event& event) const noexcept;
  [[nodiscard]] std::size_t subscriberCount() const noexcept;

 private:
  mutable std::mutex mutex_;
  SubscriptionId nextId_{1};
  std::unordered_map<SubscriptionId, Subscriber> subscribers_;
};

}  // namespace omnidetect

