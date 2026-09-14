#pragma once

#include "input/IFrameSource.h"

#include <filesystem>
#include <memory>

namespace omnidetect {

class ImageSource final : public IFrameSource {
 public:
  explicit ImageSource(std::filesystem::path path, std::string sourceId = "image");
  ~ImageSource() override;
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

