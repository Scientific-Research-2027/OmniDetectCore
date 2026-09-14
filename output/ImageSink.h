#pragma once

#include "output/IResultSink.h"

#include <filesystem>
#include <mutex>

namespace omnidetect {

class ImageSink final : public IResultSink {
 public:
  explicit ImageSink(std::filesystem::path outputPath, bool appendFrameId = false);
  bool start(std::string& error) noexcept override;
  void consume(const Frame& frame, const DetectionResult& result) noexcept override;
  [[nodiscard]] std::string name() const override { return "image"; }
  [[nodiscard]] std::string lastError() const;

 private:
  std::filesystem::path outputPath_;
  bool appendFrameId_{false};
  mutable std::mutex mutex_;
  std::string lastError_;
};

}  // namespace omnidetect

