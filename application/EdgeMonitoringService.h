#pragma once

#include "application/CameraManager.h"
#include "application/InferenceService.h"
#include "application/ResultRouter.h"
#include "config/ConfigManager.h"
#include "core/detection/IObjectDetector.h"
#include "core/monitoring/EventEngine.h"
#include "output/IResultSink.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omnidetect {

struct EdgeHealthSnapshot {
  double uptimeSeconds{0.0};
  std::size_t camerasConfigured{0};
  std::size_t camerasOnline{0};
  std::size_t camerasDegraded{0};
  std::size_t camerasOffline{0};
  InferenceServiceMetrics inference;
  SchedulerMetrics scheduler;
  ResultRouterMetrics router;
  std::vector<CameraHealthSnapshot> cameras;
};

class EdgeMonitoringService {
 public:
  EdgeMonitoringService(AppConfig config,
                        std::unique_ptr<IObjectDetector> detector,
                        CameraManager::SourceFactory sourceFactory = {},
                        CameraManager::TrackerFactory trackerFactory = {},
                        CameraManager::ControlFactory controlFactory = {});
  ~EdgeMonitoringService();

  EdgeMonitoringService(const EdgeMonitoringService&) = delete;
  EdgeMonitoringService& operator=(const EdgeMonitoringService&) = delete;

  [[nodiscard]] bool isReady() const noexcept { return initializationError_.empty(); }
  [[nodiscard]] const std::string& initializationError() const noexcept { return initializationError_; }
  bool addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept;
  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool addCamera(const CameraConfig& config, std::string& error) noexcept;
  bool removeCamera(const std::string& cameraId, std::string& error) noexcept;
  bool restartCamera(const std::string& cameraId, std::string& error) noexcept;
  bool stopCamera(const std::string& cameraId, std::string& error) noexcept;
  bool sendManualControl(const std::string& cameraId, CameraControlCommand command,
                         std::string& error) noexcept;
  [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
  [[nodiscard]] EdgeHealthSnapshot metrics() const noexcept;
  [[nodiscard]] EventEngine& events() noexcept { return events_; }

 private:
  AppConfig config_;
  EventEngine events_;
  std::shared_ptr<RoundRobinInferenceScheduler> scheduler_;
  std::unique_ptr<ResultRouter> router_;
  std::unique_ptr<CameraManager> cameras_;
  std::unique_ptr<InferenceService> inference_;
  std::string initializationError_;
  std::atomic<bool> running_{false};
  std::chrono::steady_clock::time_point startedAt_{};
};

}  // namespace omnidetect
