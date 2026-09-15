#include "application/CameraSession.h"

#include "core/logging/Logger.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <utility>

namespace omnidetect {
namespace {

constexpr std::size_t kLatencyWindow = 256;

double ageMilliseconds(const std::chrono::steady_clock::time_point value,
                       const std::chrono::steady_clock::time_point now) {
  return std::chrono::duration<double, std::milli>(now - value).count();
}

double percentile95(std::deque<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1U;
  return values[std::min(index, values.size() - 1U)];
}

}  // namespace

CameraSession::CameraSession(CameraConfig config,
                             std::unique_ptr<IFrameSource> source,
                             std::unique_ptr<IObjectTracker> tracker,
                             EventEngine& events,
                             std::unique_ptr<ICameraControl> control)
    : config_(std::move(config)),
      source_(std::move(source)),
      tracker_(std::move(tracker)),
      monitor_(config_.monitor),
      events_(events),
      frameQueue_(config_.frameQueueSize, true) {
  if (control) {
    controlWorker_ = std::make_unique<CameraControlWorker>(config_.id, config_.control,
                                                           std::move(control), events_);
    if (config_.control.autoPtz.enabled) {
      autoPtz_ = std::make_unique<AutoPtzController>(config_.id, config_.control.autoPtz,
                                                     *controlWorker_);
    }
  }
}

CameraSession::~CameraSession() { stop(); }

bool CameraSession::start(std::string& error) noexcept {
  if (captureThread_.joinable()) stop();
  const std::scoped_lock lock(lifecycleMutex_);
  if (!source_) {
    error = "Camera " + config_.id + " has no frame source";
    state_.store(CameraSessionState::Faulted, std::memory_order_release);
    return false;
  }
  if (!tracker_) {
    error = "Camera " + config_.id + " has no tracker";
    state_.store(CameraSessionState::Faulted, std::memory_order_release);
    return false;
  }
  frameQueue_.reset();
  {
    const std::scoped_lock initialLock(initialStateMutex_);
    initialAttemptComplete_ = false;
    initialConnected_ = false;
    initialConnectionError_.clear();
  }
  {
    const std::scoped_lock processingLock(processingMutex_);
    tracker_->reset();
    monitor_.reset();
    if (autoPtz_) autoPtz_->reset();
  }
  {
    const std::scoped_lock scheduleLock(scheduleMutex_);
    nextInferenceAt_ = std::chrono::steady_clock::now();
  }
  {
    const std::scoped_lock metricsLock(metricsMutex_);
    startedAt_ = std::chrono::steady_clock::now();
    hasLastFrame_ = false;
    hasLastInference_ = false;
    lastOverloadEventAt_ = {};
    framesCaptured_ = 0;
    framesScheduled_ = 0;
    framesInferred_ = 0;
    framesDropped_ = 0;
    framesSkipped_ = 0;
    framesStale_ = 0;
    inferenceErrors_ = 0;
    reconnectCount_ = 0;
    latencySamplesMs_.clear();
  }
  generation_.fetch_add(1, std::memory_order_acq_rel);
  state_.store(CameraSessionState::Starting, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  if (controlWorker_) {
    std::string controlError;
    if (!controlWorker_->start(controlError)) {
      publish(EventType::CameraControlError,
              controlError.empty() ? "Camera control adapter could not start" : controlError);
    }
  }
  try {
    captureThread_ = std::thread(&CameraSession::captureLoop, this);
  } catch (const std::exception& exception) {
    running_.store(false, std::memory_order_release);
    if (controlWorker_) controlWorker_->stop();
    state_.store(CameraSessionState::Faulted, std::memory_order_release);
    error = std::string("Cannot start capture worker for ") + config_.id + ": " + exception.what();
    return false;
  }
  error.clear();
  return true;
}

void CameraSession::stop() noexcept {
  std::unique_lock lock(lifecycleMutex_);
  const bool wasActive = running_.exchange(false, std::memory_order_acq_rel) || captureThread_.joinable();
  if (!wasActive) return;
  state_.store(CameraSessionState::Stopping, std::memory_order_release);
  generation_.fetch_add(1, std::memory_order_acq_rel);
  frameQueue_.close();
  waitCondition_.notify_all();
  initialStateCondition_.notify_all();
  lock.unlock();
  if (controlWorker_) controlWorker_->stop();
  if (captureThread_.joinable()) captureThread_.join();
  lock.lock();
  source_->close();
  frameQueue_.clear();
  {
    const std::scoped_lock processingLock(processingMutex_);
    tracker_->reset();
    monitor_.reset();
  }
  state_.store(CameraSessionState::Stopped, std::memory_order_release);
}

bool CameraSession::restart(std::string& error) noexcept {
  stop();
  return start(error);
}

bool CameraSession::waitForInitialConnection(const std::chrono::milliseconds timeout,
                                             std::string& error) noexcept {
  std::unique_lock lock(initialStateMutex_);
  if (!initialStateCondition_.wait_for(lock, timeout, [this] {
        return initialAttemptComplete_ || !running_.load(std::memory_order_acquire);
      })) {
    error = "Timed out waiting for initial connection to camera " + config_.id;
    return false;
  }
  if (!initialConnected_) {
    error = initialConnectionError_.empty()
                ? "Initial connection failed for camera " + config_.id
                : initialConnectionError_;
    return false;
  }
  error.clear();
  return true;
}

bool CameraSession::sendManualControl(CameraControlCommand command) noexcept {
  return controlWorker_ && controlWorker_->enqueueManual(command);
}

void CameraSession::setFrameNotifier(std::function<void()> notifier) {
  const std::scoped_lock lock(notifierMutex_);
  frameNotifier_ = std::move(notifier);
}

std::chrono::steady_clock::time_point CameraSession::nextInferenceEligibleAt() const noexcept {
  const std::scoped_lock lock(scheduleMutex_);
  return nextInferenceAt_;
}

std::optional<CameraFrameEnvelope> CameraSession::takeForInference(
    const std::chrono::steady_clock::time_point now, const std::chrono::milliseconds maxFrameAge) noexcept {
  {
    const std::scoped_lock lock(scheduleMutex_);
    if (now < nextInferenceAt_) return std::nullopt;
  }
  std::size_t discarded = 0;
  auto frame = frameQueue_.tryPopLatest(&discarded);
  if (!frame) return std::nullopt;

  const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - frame->frame.capturedAt);
  {
    const std::scoped_lock lock(metricsMutex_);
    framesSkipped_ += discarded;
    if (age > maxFrameAge) ++framesStale_;
  }
  if (age > maxFrameAge) return std::nullopt;

  {
    const std::scoped_lock lock(scheduleMutex_);
    const auto seconds = 1.0 / config_.source.inferenceFps;
    nextInferenceAt_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(seconds));
  }
  {
    const std::scoped_lock lock(metricsMutex_);
    ++framesScheduled_;
  }
  return std::move(*frame);
}

bool CameraSession::processResult(const Frame& frame, DetectionResult& result, const std::uint64_t generation,
                                  std::vector<Event>& events) noexcept {
  if (generation != generation_.load(std::memory_order_acquire)) return false;
  try {
    const std::scoped_lock processingLock(processingMutex_);
    if (generation != generation_.load(std::memory_order_acquire)) return false;
    result.sourceId = config_.id;
    if (config_.trackingEnabled && result.ok()) tracker_->update(result);
    if (config_.monitoringEnabled && result.ok()) events = monitor_.update(result);
    if (autoPtz_ && controlWorker_->isRunning() && result.ok()) {
      auto controlEvents = autoPtz_->update(frame, result);
      events.insert(events.end(), std::make_move_iterator(controlEvents.begin()),
                    std::make_move_iterator(controlEvents.end()));
    }
  } catch (const std::exception& exception) {
    log::error("Camera result processing failed for " + config_.id + ": " + exception.what());
    return false;
  } catch (...) {
    log::error("Camera result processing failed for " + config_.id);
    return false;
  }
  return true;
}

bool CameraSession::recordInferenceCompleted(const DetectionResult& result,
                                             const std::uint64_t generation) noexcept {
  if (generation != generation_.load(std::memory_order_acquire)) return false;
  try {
    const std::scoped_lock lock(metricsMutex_);
    if (generation != generation_.load(std::memory_order_acquire)) return false;
    ++framesInferred_;
    if (!result.ok()) ++inferenceErrors_;
    lastInferenceAt_ = std::chrono::steady_clock::now();
    hasLastInference_ = true;
    const auto latency = static_cast<double>(result.totalLatency.count()) / 1000.0;
    latencySamplesMs_.push_back(latency);
    if (latencySamplesMs_.size() > kLatencyWindow) latencySamplesMs_.pop_front();
    return true;
  } catch (...) {
    return false;
  }
}

CameraHealthSnapshot CameraSession::metrics() const noexcept {
  const auto now = std::chrono::steady_clock::now();
  const auto queueDepth = frameQueue_.size();
  const auto controlMetrics = controlWorker_ ? controlWorker_->metrics() : CameraControlMetrics{};
  const std::scoped_lock lock(metricsMutex_);
  CameraHealthSnapshot result;
  result.cameraId = config_.id;
  result.state = state();
  result.generation = generation_.load(std::memory_order_acquire);
  result.framesCaptured = framesCaptured_;
  result.framesScheduled = framesScheduled_;
  result.framesInferred = framesInferred_;
  result.framesDropped = framesDropped_;
  result.framesSkipped = framesSkipped_;
  result.framesStale = framesStale_;
  result.inferenceErrors = inferenceErrors_;
  result.reconnectCount = reconnectCount_;
  result.queueDepth = queueDepth;
  const auto uptime = std::chrono::duration<double>(now - startedAt_).count();
  if (uptime > 0.0) {
    result.captureFps = static_cast<double>(framesCaptured_) / uptime;
    result.inferenceFps = static_cast<double>(framesInferred_) / uptime;
  }
  if (!latencySamplesMs_.empty()) {
    double total = 0.0;
    for (const auto sample : latencySamplesMs_) total += sample;
    result.averageLatencyMs = total / static_cast<double>(latencySamplesMs_.size());
    result.p95LatencyMs = percentile95(latencySamplesMs_);
  }
  if (hasLastFrame_) result.lastFrameAgeMs = ageMilliseconds(lastFrameAt_, now);
  if (hasLastInference_) result.lastInferenceAgeMs = ageMilliseconds(lastInferenceAt_, now);
  result.control = controlMetrics;
  return result;
}

void CameraSession::captureLoop() noexcept {
  auto reconnectDelay = config_.reconnectInitialDelay;
  bool connectedBefore = false;
  bool initialAttempt = true;
  while (running_.load(std::memory_order_acquire)) {
    state_.store(connectedBefore ? CameraSessionState::Reconnecting : CameraSessionState::Starting,
                 std::memory_order_release);
    std::string error;
    if (!source_->open(error)) {
      if (initialAttempt) reportInitialConnection(false, error);
      initialAttempt = false;
      state_.store(CameraSessionState::Disconnected, std::memory_order_release);
      publish(EventType::CameraDisconnected, error.empty() ? "Camera source open failed" : error);
      {
        const std::scoped_lock lock(metricsMutex_);
        ++reconnectCount_;
      }
      if (waitFor(reconnectDelay)) break;
      reconnectDelay = std::min(reconnectDelay * 2, config_.reconnectMaximumDelay);
      connectedBefore = true;
      continue;
    }

    if (initialAttempt) reportInitialConnection(true, {});
    initialAttempt = false;

    if (connectedBefore) {
      resetPerConnectionState();
      publish(EventType::CameraRecovered, "Camera source recovered");
    } else {
      publish(EventType::CameraConnected, "Camera source connected");
    }
    connectedBefore = true;
    reconnectDelay = config_.reconnectInitialDelay;
    state_.store(CameraSessionState::Online, std::memory_order_release);
    auto nextCapture = std::chrono::steady_clock::now();
    unsigned temporaryFailures = 0;

    while (running_.load(std::memory_order_acquire)) {
      if (config_.source.captureFps > 0.0) {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextCapture && waitFor(nextCapture - now)) break;
      }
      auto read = source_->read();
      if (read.status == FrameReadStatus::Ready) {
        temporaryFailures = 0;
        state_.store(CameraSessionState::Online, std::memory_order_release);
        read.frame.sourceId = config_.id;
        read.frame.frameId = nextFrameId_++;
        const auto frameGeneration = generation_.load(std::memory_order_acquire);
        const auto pushed = frameQueue_.push(CameraFrameEnvelope{std::move(read.frame), frameGeneration});
        bool publishOverload = false;
        {
          const std::scoped_lock lock(metricsMutex_);
          ++framesCaptured_;
          framesDropped_ += pushed.dropped;
          lastFrameAt_ = std::chrono::steady_clock::now();
          hasLastFrame_ = true;
          if (pushed.dropped > 0 &&
              (lastOverloadEventAt_ == std::chrono::steady_clock::time_point{} ||
               lastFrameAt_ - lastOverloadEventAt_ >= std::chrono::seconds(5))) {
            lastOverloadEventAt_ = lastFrameAt_;
            publishOverload = true;
          }
        }
        if (publishOverload) {
          publish(EventType::InferenceOverload, "Frame queue dropped older frames to preserve freshness");
        }
        if (!pushed.accepted) break;
        notifyFrame();
        if (config_.source.captureFps > 0.0) {
          nextCapture = std::chrono::steady_clock::now() +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(1.0 / config_.source.captureFps));
        }
      } else if (read.status == FrameReadStatus::TemporaryError) {
        ++temporaryFailures;
        state_.store(CameraSessionState::Degraded, std::memory_order_release);
        if (temporaryFailures >= 5) break;
        if (waitFor(std::chrono::milliseconds(25))) break;
      } else if (read.status == FrameReadStatus::EndOfStream) {
        running_.store(false, std::memory_order_release);
        break;
      } else {
        publish(EventType::CameraDisconnected, read.error.empty() ? "Camera source read failed" : read.error);
        break;
      }
    }
    source_->close();
    if (!running_.load(std::memory_order_acquire)) break;
    publish(EventType::CameraDisconnected, "Camera source will reconnect after repeated read failures");
    resetPerConnectionState();
    state_.store(CameraSessionState::Disconnected, std::memory_order_release);
    {
      const std::scoped_lock lock(metricsMutex_);
      ++reconnectCount_;
    }
    if (waitFor(reconnectDelay)) break;
    reconnectDelay = std::min(reconnectDelay * 2, config_.reconnectMaximumDelay);
  }
  source_->close();
  if (state_.load(std::memory_order_acquire) != CameraSessionState::Stopping) {
    state_.store(CameraSessionState::Stopped, std::memory_order_release);
  }
}

void CameraSession::resetPerConnectionState() noexcept {
  generation_.fetch_add(1, std::memory_order_acq_rel);
  frameQueue_.clear();
  const std::scoped_lock lock(processingMutex_);
  tracker_->reset();
  monitor_.reset();
  if (autoPtz_) autoPtz_->reset();
}

bool CameraSession::waitFor(const std::chrono::steady_clock::duration duration) noexcept {
  std::unique_lock lock(waitMutex_);
  return waitCondition_.wait_for(lock, duration, [this] { return !running_.load(std::memory_order_acquire); });
}

void CameraSession::notifyFrame() noexcept {
  std::function<void()> notifier;
  {
    const std::scoped_lock lock(notifierMutex_);
    notifier = frameNotifier_;
  }
  if (!notifier) return;
  try {
    notifier();
  } catch (const std::exception& exception) {
    log::error("Frame notifier failed for " + config_.id + ": " + exception.what());
  } catch (...) {
    log::error("Frame notifier failed for " + config_.id);
  }
}

void CameraSession::reportInitialConnection(const bool connected, std::string error) noexcept {
  {
    const std::scoped_lock lock(initialStateMutex_);
    if (initialAttemptComplete_) return;
    initialAttemptComplete_ = true;
    initialConnected_ = connected;
    initialConnectionError_ = std::move(error);
  }
  initialStateCondition_.notify_all();
}

void CameraSession::publish(const EventType type, const std::string& message) noexcept {
  events_.publish({type, std::chrono::system_clock::now(), 0, config_.id, std::nullopt, message, {}});
}

}  // namespace omnidetect
