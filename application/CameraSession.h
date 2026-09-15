#pragma once

#include "application/LatestValueQueue.h"
#include "config/ConfigManager.h"
#include "control/AutoPtzController.h"
#include "control/CameraControlWorker.h"
#include "core/monitoring/EventEngine.h"
#include "core/monitoring/MultiObjectMonitor.h"
#include "core/tracking/ObjectTracker.h"
#include "input/IFrameSource.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace omnidetect {

enum class CameraSessionState {
  Stopped,
  Starting,
  Online,
  Degraded,
  Disconnected,
  Reconnecting,
  Stopping,
  Faulted
};

struct CameraHealthSnapshot {
  std::string cameraId;
  CameraSessionState state{CameraSessionState::Stopped};
  std::uint64_t generation{0};
  std::uint64_t framesCaptured{0};
  std::uint64_t framesScheduled{0};
  std::uint64_t framesInferred{0};
  std::uint64_t framesDropped{0};
  std::uint64_t framesSkipped{0};
  std::uint64_t framesStale{0};
  std::uint64_t inferenceErrors{0};
  std::uint64_t reconnectCount{0};
  std::size_t queueDepth{0};
  double captureFps{0.0};
  double inferenceFps{0.0};
  double averageLatencyMs{0.0};
  double p95LatencyMs{0.0};
  double lastFrameAgeMs{-1.0};
  double lastInferenceAgeMs{-1.0};
  CameraControlMetrics control;
};

struct CameraFrameEnvelope {
  Frame frame;
  std::uint64_t generation{0};
};

class CameraSession {
 public:
  CameraSession(CameraConfig config,
                std::unique_ptr<IFrameSource> source,
                std::unique_ptr<IObjectTracker> tracker,
                EventEngine& events,
                std::unique_ptr<ICameraControl> control = {});
  ~CameraSession();

  CameraSession(const CameraSession&) = delete;
  CameraSession& operator=(const CameraSession&) = delete;

  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool restart(std::string& error) noexcept;
  bool waitForInitialConnection(std::chrono::milliseconds timeout, std::string& error) noexcept;
  bool sendManualControl(CameraControlCommand command) noexcept;
  void setFrameNotifier(std::function<void()> notifier);

  [[nodiscard]] const std::string& id() const noexcept { return config_.id; }
  [[nodiscard]] const CameraConfig& config() const noexcept { return config_; }
  [[nodiscard]] CameraSessionState state() const noexcept { return state_.load(std::memory_order_acquire); }
  [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
  [[nodiscard]] bool hasPendingFrame() const noexcept { return frameQueue_.size() > 0; }
  [[nodiscard]] std::chrono::steady_clock::time_point nextInferenceEligibleAt() const noexcept;
  [[nodiscard]] std::optional<CameraFrameEnvelope> takeForInference(
      std::chrono::steady_clock::time_point now, std::chrono::milliseconds maxFrameAge) noexcept;

  bool recordInferenceCompleted(const DetectionResult& result, std::uint64_t generation) noexcept;
  bool processResult(const Frame& frame, DetectionResult& result, std::uint64_t generation,
                     std::vector<Event>& events) noexcept;
  [[nodiscard]] CameraHealthSnapshot metrics() const noexcept;

 private:
  CameraConfig config_;
  std::unique_ptr<IFrameSource> source_;
  std::unique_ptr<IObjectTracker> tracker_;
  MultiObjectMonitor monitor_;
  std::unique_ptr<CameraControlWorker> controlWorker_;
  std::unique_ptr<AutoPtzController> autoPtz_;
  EventEngine& events_;
  LatestValueQueue<CameraFrameEnvelope> frameQueue_;

  std::atomic<CameraSessionState> state_{CameraSessionState::Stopped};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> generation_{0};
  std::thread captureThread_;
  std::uint64_t nextFrameId_{1};
  mutable std::mutex lifecycleMutex_;
  mutable std::mutex waitMutex_;
  std::condition_variable waitCondition_;
  mutable std::mutex initialStateMutex_;
  std::condition_variable initialStateCondition_;
  bool initialAttemptComplete_{false};
  bool initialConnected_{false};
  std::string initialConnectionError_;
  mutable std::mutex notifierMutex_;
  std::function<void()> frameNotifier_;
  mutable std::mutex processingMutex_;
  mutable std::mutex scheduleMutex_;
  std::chrono::steady_clock::time_point nextInferenceAt_{};

  mutable std::mutex metricsMutex_;
  std::chrono::steady_clock::time_point startedAt_{};
  std::chrono::steady_clock::time_point lastFrameAt_{};
  std::chrono::steady_clock::time_point lastInferenceAt_{};
  std::chrono::steady_clock::time_point lastOverloadEventAt_{};
  bool hasLastFrame_{false};
  bool hasLastInference_{false};
  std::uint64_t framesCaptured_{0};
  std::uint64_t framesScheduled_{0};
  std::uint64_t framesInferred_{0};
  std::uint64_t framesDropped_{0};
  std::uint64_t framesSkipped_{0};
  std::uint64_t framesStale_{0};
  std::uint64_t inferenceErrors_{0};
  std::uint64_t reconnectCount_{0};
  std::deque<double> latencySamplesMs_;

  void captureLoop() noexcept;
  void resetPerConnectionState() noexcept;
  bool waitFor(std::chrono::steady_clock::duration duration) noexcept;
  void notifyFrame() noexcept;
  void reportInitialConnection(bool connected, std::string error) noexcept;
  void publish(EventType type, const std::string& message) noexcept;
};

}  // namespace omnidetect
