#pragma once

#include "core/detection/Detection.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace omnidetect {

enum class DetectionStatus { Ok, NoDetections, InvalidFrame, ModelNotReady, BackendError, InvalidOutput };

struct DetectionResult {
  std::uint64_t frameId{0};
  std::string sourceId;
  std::vector<Detection> detections;
  std::chrono::microseconds preprocessTime{0};
  std::chrono::microseconds inferenceTime{0};
  std::chrono::microseconds postprocessTime{0};
  std::chrono::microseconds totalLatency{0};
  DetectionStatus status{DetectionStatus::Ok};
  std::string error;

  [[nodiscard]] bool ok() const noexcept {
    return status == DetectionStatus::Ok || status == DetectionStatus::NoDetections;
  }
};

}  // namespace omnidetect

