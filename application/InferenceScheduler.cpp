#include "application/InferenceScheduler.h"

#include <algorithm>
#include <limits>

namespace omnidetect {

RoundRobinInferenceScheduler::RoundRobinInferenceScheduler(const std::chrono::milliseconds maxFrameAge)
    : maxFrameAge_(maxFrameAge) {}

bool RoundRobinInferenceScheduler::addCamera(const std::shared_ptr<CameraSession>& session, std::string& error) {
  if (!session) {
    error = "Cannot register a null camera session";
    return false;
  }
  const std::scoped_lock lock(mutex_);
  for (const auto& weak : cameras_) {
    if (const auto camera = weak.lock(); camera && camera->id() == session->id()) {
      error = "Camera is already registered with scheduler: " + session->id();
      return false;
    }
  }
  cameras_.push_back(session);
  ++version_;
  condition_.notify_all();
  error.clear();
  return true;
}

bool RoundRobinInferenceScheduler::removeCamera(const std::string& cameraId) noexcept {
  const std::scoped_lock lock(mutex_);
  const auto oldSize = cameras_.size();
  cameras_.erase(std::remove_if(cameras_.begin(), cameras_.end(), [&cameraId](const auto& weak) {
                   const auto camera = weak.lock();
                   return !camera || camera->id() == cameraId;
                 }),
                 cameras_.end());
  cursor_ = 0;
  if (cameras_.size() != oldSize) {
    ++version_;
    condition_.notify_all();
    return true;
  }
  return false;
}

void RoundRobinInferenceScheduler::start() noexcept {
  const std::scoped_lock lock(mutex_);
  running_ = true;
  ++version_;
  condition_.notify_all();
}

void RoundRobinInferenceScheduler::stop() noexcept {
  const std::scoped_lock lock(mutex_);
  running_ = false;
  ++version_;
  condition_.notify_all();
}

void RoundRobinInferenceScheduler::notifyFrameAvailable() noexcept {
  const std::scoped_lock lock(mutex_);
  ++notifications_;
  ++version_;
  condition_.notify_one();
}

std::optional<ScheduledFrame> RoundRobinInferenceScheduler::next() noexcept {
  for (;;) {
    std::vector<std::shared_ptr<CameraSession>> cameras;
    std::size_t start = 0;
    std::uint64_t observedVersion = 0;
    {
      std::unique_lock lock(mutex_);
      if (!running_) return std::nullopt;
      cameras_.erase(std::remove_if(cameras_.begin(), cameras_.end(),
                                    [](const auto& weak) { return weak.expired(); }),
                     cameras_.end());
      for (const auto& weak : cameras_) {
        if (auto camera = weak.lock()) {
          const auto weight = static_cast<std::size_t>(std::clamp(camera->config().priority, 1, 16));
          for (std::size_t slot = 0; slot < weight; ++slot) cameras.push_back(camera);
        }
      }
      if (cameras.empty()) {
        observedVersion = version_;
        condition_.wait(lock, [this, observedVersion] { return !running_ || version_ != observedVersion; });
        if (!running_) return std::nullopt;
        continue;
      }
      cursor_ %= cameras.size();
      start = cursor_;
      observedVersion = version_;
    }

    const auto now = std::chrono::steady_clock::now();
    auto nextWake = std::chrono::steady_clock::time_point::max();
    bool pendingFrame = false;
    for (std::size_t offset = 0; offset < cameras.size(); ++offset) {
      const auto index = (start + offset) % cameras.size();
      auto& camera = cameras[index];
      if (auto envelope = camera->takeForInference(now, maxFrameAge_)) {
        const std::scoped_lock lock(mutex_);
        cursor_ = (index + 1U) % cameras.size();
        ++scheduledFrames_;
        return ScheduledFrame{camera, std::move(envelope->frame), envelope->generation};
      }
      if (camera->hasPendingFrame()) {
        pendingFrame = true;
        nextWake = std::min(nextWake, camera->nextInferenceEligibleAt());
      }
    }

    std::unique_lock lock(mutex_);
    if (pendingFrame) {
      condition_.wait_until(lock, nextWake,
                            [this, observedVersion] { return !running_ || version_ != observedVersion; });
    } else {
      condition_.wait(lock, [this, observedVersion] { return !running_ || version_ != observedVersion; });
    }
    if (!running_) return std::nullopt;
  }
}

SchedulerMetrics RoundRobinInferenceScheduler::metrics() const noexcept {
  const std::scoped_lock lock(mutex_);
  SchedulerMetrics result;
  result.scheduledFrames = scheduledFrames_;
  result.notifications = notifications_;
  result.registeredCameras = static_cast<std::size_t>(std::count_if(
      cameras_.begin(), cameras_.end(), [](const auto& weak) { return !weak.expired(); }));
  result.running = running_;
  return result;
}

}  // namespace omnidetect
