#pragma once

#include "core/frame/Frame.h"

#include <string>

namespace omnidetect {

enum class FrameReadStatus { Ready, EndOfStream, TemporaryError, FatalError };

struct FrameReadResult {
  FrameReadStatus status{FrameReadStatus::FatalError};
  Frame frame;
  std::string error;
};

struct SourceMetadata {
  std::string id;
  std::string kind;
  int width{0};
  int height{0};
  double fps{0.0};
};

class IFrameSource {
 public:
  virtual ~IFrameSource() = default;
  virtual bool open(std::string& error) noexcept = 0;
  virtual FrameReadResult read() noexcept = 0;
  virtual void close() noexcept = 0;
  [[nodiscard]] virtual bool isOpened() const noexcept = 0;
  [[nodiscard]] virtual SourceMetadata metadata() const = 0;
};

}  // namespace omnidetect

