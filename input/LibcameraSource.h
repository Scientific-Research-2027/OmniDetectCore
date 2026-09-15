#pragma once

#include "config/ConfigManager.h"
#include "input/IFrameSource.h"

#include <memory>

namespace omnidetect {

class LibcameraSource final : public IFrameSource {
 public:
  explicit LibcameraSource(SourceConfig config);
  ~LibcameraSource() override;

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
