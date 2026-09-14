#pragma once

#include "output/IResultSink.h"

#include <filesystem>
#include <memory>

namespace omnidetect {

struct RecorderConfig {
  std::filesystem::path path;
  double fps{25.0};
  std::uintmax_t maxBytes{0};
};

class RecorderSink final : public IResultSink {
 public:
  explicit RecorderSink(RecorderConfig config);
  ~RecorderSink() override;
  bool start(std::string& error) noexcept override;
  void consume(const Frame& frame, const DetectionResult& result) noexcept override;
  void stop() noexcept override;
  [[nodiscard]] std::string name() const override { return "recorder"; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace omnidetect

