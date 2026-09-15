#pragma once

#include "application/CameraSession.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace omnidetect {

struct ScheduledFrame {
  std::shared_ptr<CameraSession> session;
  Frame frame;
  std::uint64_t generation{0};
};

struct SchedulerMetrics {
  std::uint64_t scheduledFrames{0};
  std::uint64_t notifications{0};
  std::size_t registeredCameras{0};
  bool running{false};
};

class IInferenceScheduler {
 public:
  virtual ~IInferenceScheduler() = default;
  virtual bool addCamera(const std::shared_ptr<CameraSession>& session, std::string& error) = 0;
  virtual bool removeCamera(const std::string& cameraId) noexcept = 0;
  virtual void start() noexcept = 0;
  virtual void stop() noexcept = 0;
  virtual void notifyFrameAvailable() noexcept = 0;
  [[nodiscard]] virtual std::optional<ScheduledFrame> next() noexcept = 0;
  [[nodiscard]] virtual SchedulerMetrics metrics() const noexcept = 0;
};

class RoundRobinInferenceScheduler final : public IInferenceScheduler {
 public:
  explicit RoundRobinInferenceScheduler(std::chrono::milliseconds maxFrameAge);

  bool addCamera(const std::shared_ptr<CameraSession>& session, std::string& error) override;
  bool removeCamera(const std::string& cameraId) noexcept override;
  void start() noexcept override;
  void stop() noexcept override;
  void notifyFrameAvailable() noexcept override;
  [[nodiscard]] std::optional<ScheduledFrame> next() noexcept override;
  [[nodiscard]] SchedulerMetrics metrics() const noexcept override;

 private:
  const std::chrono::milliseconds maxFrameAge_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<std::weak_ptr<CameraSession>> cameras_;
  std::size_t cursor_{0};
  bool running_{false};
  std::uint64_t version_{0};
  std::uint64_t scheduledFrames_{0};
  std::uint64_t notifications_{0};
};

}  // namespace omnidetect

