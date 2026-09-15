#pragma once

#include "application/CameraSession.h"
#include "application/InferenceScheduler.h"
#include "config/ConfigManager.h"
#include "core/monitoring/EventEngine.h"
#include "control/ICameraControl.h"
#include "input/IFrameSource.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnidetect {

class CameraManager {
 public:
  using SourceFactory = std::function<std::unique_ptr<IFrameSource>(const SourceConfig&, std::string&)>;
  using TrackerFactory = std::function<std::unique_ptr<IObjectTracker>(const TrackerConfig&)>;
  using ControlFactory = std::function<std::unique_ptr<ICameraControl>(const CameraConfig&, std::string&)>;

  CameraManager(std::shared_ptr<IInferenceScheduler> scheduler,
                EventEngine& events,
                SourceFactory sourceFactory,
                TrackerFactory trackerFactory = {},
                ControlFactory controlFactory = {});
  ~CameraManager();

  bool configure(const std::vector<CameraConfig>& cameras, std::string& error);
  bool addCamera(const CameraConfig& config, std::string& error);
  bool removeCamera(const std::string& cameraId, std::string& error) noexcept;
  bool startAll(bool failFast, std::string& error) noexcept;
  void stopAll() noexcept;
  bool restartCamera(const std::string& cameraId, std::string& error) noexcept;
  bool stopCamera(const std::string& cameraId, std::string& error) noexcept;
  bool sendManualControl(const std::string& cameraId, CameraControlCommand command,
                         std::string& error) noexcept;
  [[nodiscard]] std::shared_ptr<CameraSession> find(const std::string& cameraId) const noexcept;
  [[nodiscard]] std::vector<CameraHealthSnapshot> metrics() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::shared_ptr<IInferenceScheduler> scheduler_;
  EventEngine& events_;
  SourceFactory sourceFactory_;
  TrackerFactory trackerFactory_;
  ControlFactory controlFactory_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<CameraSession>> sessions_;
};

}  // namespace omnidetect
