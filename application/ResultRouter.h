#pragma once

#include "application/CameraSession.h"
#include "application/LatestValueQueue.h"
#include "core/monitoring/EventEngine.h"
#include "output/IResultSink.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace omnidetect {

struct ResultRouterMetrics {
  std::uint64_t enqueued{0};
  std::uint64_t delivered{0};
  std::uint64_t dropped{0};
  std::size_t queueDepth{0};
};

class ResultRouter {
 public:
  ResultRouter(std::size_t queueCapacity, EventEngine& events);
  ~ResultRouter();

  ResultRouter(const ResultRouter&) = delete;
  ResultRouter& operator=(const ResultRouter&) = delete;

  bool addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept;
  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool enqueue(std::shared_ptr<CameraSession> session, Frame frame, DetectionResult result,
               std::uint64_t generation) noexcept;
  [[nodiscard]] ResultRouterMetrics metrics() const noexcept;

 private:
  struct Packet {
    std::shared_ptr<CameraSession> session;
    Frame frame;
    DetectionResult result;
    std::uint64_t generation{0};
  };

  EventEngine& events_;
  LatestValueQueue<Packet> queue_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  mutable std::mutex lifecycleMutex_;
  mutable std::mutex sinksMutex_;
  std::vector<std::shared_ptr<IResultSink>> sinks_;
  std::atomic<std::uint64_t> enqueued_{0};
  std::atomic<std::uint64_t> delivered_{0};
  std::atomic<std::uint64_t> dropped_{0};

  void deliveryLoop() noexcept;
};

}  // namespace omnidetect

