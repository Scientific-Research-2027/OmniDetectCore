#include "core/monitoring/EventEngine.h"

#include "core/logging/Logger.h"

#include <exception>
#include <vector>

namespace omnidetect {

EventEngine::SubscriptionId EventEngine::subscribe(Subscriber subscriber) {
  if (!subscriber) return 0;
  const std::scoped_lock lock(mutex_);
  const auto id = nextId_++;
  subscribers_.emplace(id, std::move(subscriber));
  return id;
}

bool EventEngine::unsubscribe(const SubscriptionId id) noexcept {
  const std::scoped_lock lock(mutex_);
  return subscribers_.erase(id) > 0;
}

void EventEngine::publish(const Event& event) const noexcept {
  std::vector<Subscriber> snapshot;
  {
    const std::scoped_lock lock(mutex_);
    snapshot.reserve(subscribers_.size());
    for (const auto& [unused, subscriber] : subscribers_) {
      static_cast<void>(unused);
      snapshot.push_back(subscriber);
    }
  }
  for (const auto& subscriber : snapshot) {
    try {
      subscriber(event);
    } catch (const std::exception& exception) {
      log::error(std::string("Event subscriber failed: ") + exception.what());
    } catch (...) {
      log::error("Event subscriber failed with an unknown error");
    }
  }
}

std::size_t EventEngine::subscriberCount() const noexcept {
  const std::scoped_lock lock(mutex_);
  return subscribers_.size();
}

}  // namespace omnidetect

