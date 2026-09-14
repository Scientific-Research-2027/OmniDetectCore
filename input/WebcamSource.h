#pragma once

#include "input/IFrameSource.h"

#include <memory>

namespace omnidetect {

struct WebcamConfig {
  int deviceIndex{0};
  int width{1280};
  int height{720};
  double fps{0.0};
  std::string sourceId{"webcam-0"};
};

class WebcamSource final : public IFrameSource {
 public:
  explicit WebcamSource(WebcamConfig config = {});
  ~WebcamSource() override;
  bool open(std::string& error) noexcept override;
  FrameReadResult read() noexcept override;
  void close() noexcept override;
  [[nodiscard]] bool isOpened() const noexcept override;
  [[nodiscard]] SourceMetadata metadata() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace omnidetect

