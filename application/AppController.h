#pragma once

#include "application/DetectionPipeline.h"

#include <memory>
#include <mutex>
#include <string>

namespace omnidetect {

class AppController {
 public:
  AppController() = default;
  ~AppController();

  bool setPipeline(std::unique_ptr<DetectionPipeline> pipeline, std::string& error) noexcept;
  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool refresh(std::string& error) noexcept;
  [[nodiscard]] bool isRunning() const noexcept;
  [[nodiscard]] PipelineMetrics metrics() const noexcept;
  [[nodiscard]] DetectionPipeline* pipeline() noexcept;

 private:
  mutable std::mutex mutex_;
  std::unique_ptr<DetectionPipeline> pipeline_;
};

}  // namespace omnidetect

