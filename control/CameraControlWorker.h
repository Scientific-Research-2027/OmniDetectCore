#pragma once

#include "config/ConfigManager.h"
#include "control/ICameraControl.h"
#include "core/monitoring/EventEngine.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace omnidetect {

struct CameraControlMetrics {
  std::uint64_t commandsQueued{0};
  std::uint64_t commandsExecuted{0};
  std::uint64_t commandsDropped{0};
  std::uint64_t errors{0};
  std::size_t queueDepth{0};
  bool running{false};
  bool manualOverride{false};
};

class CameraControlWorker {
 public:
  CameraControlWorker(std::string cameraId, CameraControlConfig config,
                      std::unique_ptr<ICameraControl> control, EventEngine& events);
  ~CameraControlWorker();

  CameraControlWorker(const CameraControlWorker&) = delete;
  CameraControlWorker& operator=(const CameraControlWorker&) = delete;

  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool enqueueAutomatic(CameraControlCommand command) noexcept;
  bool enqueueManual(CameraControlCommand command) noexcept;
  [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
  [[nodiscard]] bool manualOverrideActive() const noexcept;
  [[nodiscard]] CameraControlMetrics metrics() const noexcept;

 private:
  struct QueuedCommand {
    CameraControlCommand command;
    CameraCommandOrigin origin{CameraCommandOrigin::Automatic};
  };

  std::string cameraId_;
  CameraControlConfig config_;
  std::unique_ptr<ICameraControl> control_;
  EventEngine& events_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<QueuedCommand> queue_;
  std::chrono::steady_clock::time_point manualOverrideUntil_{};
  std::uint64_t commandsQueued_{0};
  std::uint64_t commandsExecuted_{0};
  std::uint64_t commandsDropped_{0};
  std::uint64_t errors_{0};

  bool enqueue(CameraControlCommand command, CameraCommandOrigin origin) noexcept;
  void run() noexcept;
  void publish(EventType type, const std::string& message) noexcept;
};

}  // namespace omnidetect
