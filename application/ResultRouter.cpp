#include "application/ResultRouter.h"

#include "core/logging/Logger.h"

#include <chrono>
#include <exception>
#include <utility>

namespace omnidetect {

ResultRouter::ResultRouter(const std::size_t queueCapacity, EventEngine& events)
    : events_(events), queue_(queueCapacity, true) {}

ResultRouter::~ResultRouter() { stop(); }

bool ResultRouter::addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept {
  if (!sink) {
    error = "Cannot add a null result sink";
    return false;
  }
  if (running_.load(std::memory_order_acquire)) {
    error = "Result sinks must be added before the router starts";
    return false;
  }
  const std::scoped_lock lock(sinksMutex_);
  sinks_.push_back(std::move(sink));
  error.clear();
  return true;
}

bool ResultRouter::start(std::string& error) noexcept {
  if (worker_.joinable()) stop();
  const std::scoped_lock lock(lifecycleMutex_);
  std::vector<std::shared_ptr<IResultSink>> sinks;
  {
    const std::scoped_lock sinksLock(sinksMutex_);
    sinks = sinks_;
  }
  std::size_t started = 0;
  for (const auto& sink : sinks) {
    if (!sink->start(error)) {
      for (std::size_t index = 0; index < started; ++index) sinks[index]->stop();
      error = "Failed to start result sink " + sink->name() + ": " + error;
      return false;
    }
    ++started;
  }
  queue_.reset();
  enqueued_.store(0);
  delivered_.store(0);
  dropped_.store(0);
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread(&ResultRouter::deliveryLoop, this);
  } catch (const std::exception& exception) {
    running_.store(false, std::memory_order_release);
    queue_.close();
    for (const auto& sink : sinks) sink->stop();
    error = std::string("Cannot start result router: ") + exception.what();
    return false;
  }
  error.clear();
  return true;
}

void ResultRouter::stop() noexcept {
  std::unique_lock lock(lifecycleMutex_);
  const bool wasActive = running_.exchange(false, std::memory_order_acq_rel) || worker_.joinable();
  if (!wasActive) return;
  queue_.close();
  lock.unlock();
  if (worker_.joinable()) worker_.join();
  std::vector<std::shared_ptr<IResultSink>> sinks;
  {
    const std::scoped_lock sinksLock(sinksMutex_);
    sinks = sinks_;
  }
  for (const auto& sink : sinks) sink->stop();
}

bool ResultRouter::enqueue(std::shared_ptr<CameraSession> session, Frame frame, DetectionResult result,
                           const std::uint64_t generation) noexcept {
  if (!running_.load(std::memory_order_acquire) || !session) return false;
  try {
    const auto pushed = queue_.push(Packet{std::move(session), std::move(frame), std::move(result), generation});
    if (pushed.accepted) ++enqueued_;
    dropped_.fetch_add(pushed.dropped);
    return pushed.accepted;
  } catch (const std::exception& exception) {
    log::error(std::string("Cannot enqueue inference result: ") + exception.what());
    return false;
  } catch (...) {
    log::error("Cannot enqueue inference result");
    return false;
  }
}

ResultRouterMetrics ResultRouter::metrics() const noexcept {
  return {enqueued_.load(), delivered_.load(), dropped_.load(), queue_.size()};
}

void ResultRouter::deliveryLoop() noexcept {
  for (;;) {
    auto packet = queue_.pop();
    if (!packet) break;
    std::vector<Event> monitorEvents;
    if (!packet->session->processResult(packet->frame, packet->result, packet->generation, monitorEvents)) continue;
    if (!packet->result.ok()) {
      events_.publish({EventType::ModelError, std::chrono::system_clock::now(), packet->result.frameId,
                       packet->result.sourceId, std::nullopt, packet->result.error, {}});
    }
    for (const auto& event : monitorEvents) events_.publish(event);

    std::vector<std::shared_ptr<IResultSink>> sinks;
    {
      const std::scoped_lock lock(sinksMutex_);
      sinks = sinks_;
    }
    for (const auto& sink : sinks) {
      try {
        sink->consume(packet->frame, packet->result);
      } catch (const std::exception& exception) {
        log::error("Result sink " + sink->name() + " threw: " + exception.what());
      } catch (...) {
        log::error("Result sink " + sink->name() + " threw an unknown error");
      }
    }
    ++delivered_;
  }
}

}  // namespace omnidetect
