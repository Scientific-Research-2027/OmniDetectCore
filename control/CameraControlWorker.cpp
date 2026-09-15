#include "control/CameraControlWorker.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace omnidetect {

CameraControlWorker::CameraControlWorker(std::string cameraId, CameraControlConfig config,
                                         std::unique_ptr<ICameraControl> control, EventEngine& events)
    : cameraId_(std::move(cameraId)), config_(std::move(config)), control_(std::move(control)), events_(events) {}

CameraControlWorker::~CameraControlWorker() { stop(); }

bool CameraControlWorker::start(std::string& error) noexcept {
  if (worker_.joinable()) stop();
  if (!control_) {
    error = "No camera control adapter is configured for " + cameraId_;
    return false;
  }
  if (config_.queueSize == 0 || config_.maximumCommandsPerSecond <= 0.0) {
    error = "Camera control queue/rate limits are invalid for " + cameraId_;
    return false;
  }
  if (!control_->open(error)) return false;
  {
    const std::scoped_lock lock(mutex_);
    queue_.clear();
    commandsQueued_ = commandsExecuted_ = commandsDropped_ = errors_ = 0;
    manualOverrideUntil_ = {};
  }
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread(&CameraControlWorker::run, this);
  } catch (const std::exception& exception) {
    running_.store(false, std::memory_order_release);
    control_->close();
    error = std::string("Cannot start camera control worker: ") + exception.what();
    return false;
  }
  error.clear();
  return true;
}

void CameraControlWorker::stop() noexcept {
  const bool wasActive = running_.exchange(false, std::memory_order_acq_rel) || worker_.joinable();
  if (!wasActive) return;
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();
  if (control_ && control_->isReady()) {
    std::string ignored;
    control_->execute({CameraCommandType::Stop}, ignored);
  }
  if (control_) control_->close();
  const std::scoped_lock lock(mutex_);
  queue_.clear();
}

bool CameraControlWorker::enqueueAutomatic(CameraControlCommand command) noexcept {
  return enqueue(command, CameraCommandOrigin::Automatic);
}

bool CameraControlWorker::enqueueManual(CameraControlCommand command) noexcept {
  return enqueue(command, CameraCommandOrigin::Manual);
}

bool CameraControlWorker::enqueue(CameraControlCommand command, const CameraCommandOrigin origin) noexcept {
  if (!running_.load(std::memory_order_acquire)) return false;
  const auto now = std::chrono::steady_clock::now();
  const std::scoped_lock lock(mutex_);
  if (origin == CameraCommandOrigin::Automatic && now < manualOverrideUntil_) return false;
  if (origin == CameraCommandOrigin::Manual) {
    manualOverrideUntil_ = now + config_.manualOverrideHold;
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const auto& queued) {
                   return queued.origin == CameraCommandOrigin::Automatic;
                 }), queue_.end());
  }
  if (command.type == CameraCommandType::Stop) {
    commandsDropped_ += queue_.size();
    queue_.clear();
    queue_.push_front({command, origin});
  } else {
    const auto oldSize = queue_.size();
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [origin](const auto& queued) {
                   return queued.origin == origin && queued.command.type != CameraCommandType::Stop;
                 }), queue_.end());
    commandsDropped_ += oldSize - queue_.size();
    while (queue_.size() >= config_.queueSize) {
      queue_.pop_front();
      ++commandsDropped_;
    }
    queue_.push_back({command, origin});
  }
  ++commandsQueued_;
  condition_.notify_one();
  return true;
}

bool CameraControlWorker::manualOverrideActive() const noexcept {
  const std::scoped_lock lock(mutex_);
  return std::chrono::steady_clock::now() < manualOverrideUntil_;
}

CameraControlMetrics CameraControlWorker::metrics() const noexcept {
  const std::scoped_lock lock(mutex_);
  return {commandsQueued_, commandsExecuted_, commandsDropped_, errors_, queue_.size(),
          running_.load(std::memory_order_acquire),
          std::chrono::steady_clock::now() < manualOverrideUntil_};
}

void CameraControlWorker::run() noexcept {
  auto nextAllowed = std::chrono::steady_clock::now();
  while (running_.load(std::memory_order_acquire)) {
    QueuedCommand queued;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, [this] { return !running_.load(std::memory_order_acquire) || !queue_.empty(); });
      if (!running_.load(std::memory_order_acquire)) break;
      if (queue_.front().command.type != CameraCommandType::Stop) {
        condition_.wait_until(lock, nextAllowed, [this] {
          return !running_.load(std::memory_order_acquire) ||
                 (!queue_.empty() && queue_.front().command.type == CameraCommandType::Stop);
        });
        if (!running_.load(std::memory_order_acquire)) break;
      }
      queued = queue_.front();
      queue_.pop_front();
    }
    std::string error;
    const bool success = control_->execute(queued.command, error);
    {
      const std::scoped_lock lock(mutex_);
      if (success) ++commandsExecuted_;
      else ++errors_;
    }
    if (!success) publish(EventType::CameraControlError,
                          error.empty() ? "Camera control command failed" : error);
    else if (queued.command.type == CameraCommandType::Stop) publish(EventType::PtzStopped, "PTZ stopped");
    else publish(EventType::PtzStarted, "PTZ command applied");
    nextAllowed = std::chrono::steady_clock::now() +
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>(1.0 / config_.maximumCommandsPerSecond));
  }
}

void CameraControlWorker::publish(const EventType type, const std::string& message) noexcept {
  events_.publish({type, std::chrono::system_clock::now(), 0, cameraId_, std::nullopt, message, {}});
}

}  // namespace omnidetect
