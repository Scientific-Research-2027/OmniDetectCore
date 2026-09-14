#pragma once

#include "core/detection/DetectionResult.h"
#include "core/frame/Frame.h"

#include <string>

namespace omnidetect {

class IResultSink {
 public:
  virtual ~IResultSink() = default;
  virtual bool start(std::string& error) noexcept {
    error.clear();
    return true;
  }
  virtual void consume(const Frame& frame, const DetectionResult& result) noexcept = 0;
  virtual void stop() noexcept {}
  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace omnidetect

