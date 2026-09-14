#pragma once

#include "core/detection/DetectionResult.h"
#include "core/frame/Frame.h"

#include <string>

namespace omnidetect {

class IObjectDetector {
 public:
  virtual ~IObjectDetector() = default;
  [[nodiscard]] virtual bool isReady() const noexcept = 0;
  [[nodiscard]] virtual std::string name() const = 0;
  virtual DetectionResult detect(const Frame& frame) noexcept = 0;
};

}  // namespace omnidetect

