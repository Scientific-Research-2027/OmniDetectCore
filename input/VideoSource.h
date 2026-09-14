#pragma once

#include "input/IFrameSource.h"

#include <filesystem>
#include <memory>

namespace omnidetect {

struct VideoSourceConfig {
  std::filesystem::path path;
  bool loop{false};
  std::string sourceId{"video"};
};

class VideoSource final : public IFrameSource {
 public:
  explicit VideoSource(VideoSourceConfig config);
  ~VideoSource() override;
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

