#pragma once

#include "config/ConfigManager.h"
#include "control/CameraControlWorker.h"
#include "core/detection/DetectionResult.h"
#include "core/frame/Frame.h"
#include "core/monitoring/Event.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace omnidetect {

class AutoPtzController {
 public:
  AutoPtzController(std::string cameraId, AutoPtzConfig config,
                    CameraControlWorker& worker);

  [[nodiscard]] std::vector<Event> update(const Frame& frame, const DetectionResult& result);
  void reset() noexcept;
  [[nodiscard]] std::optional<std::uint64_t> targetTrackId() const noexcept { return targetTrackId_; }

 private:
  std::string cameraId_;
  AutoPtzConfig config_;
  CameraControlWorker& worker_;
  std::optional<std::uint64_t> targetTrackId_;
  std::chrono::steady_clock::time_point targetLockedAt_{};
  std::chrono::steady_clock::time_point lastSeenAt_{};
  std::chrono::steady_clock::time_point nextCommandAt_{};
  bool moving_{false};
  bool panActive_{false};
  bool tiltActive_{false};

  [[nodiscard]] int priorityOf(const Detection& detection) const noexcept;
  [[nodiscard]] float score(const Detection& detection, int width, int height) const noexcept;
};

}  // namespace omnidetect
