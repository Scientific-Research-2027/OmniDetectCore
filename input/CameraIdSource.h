#pragma once

#include "input/IFrameSource.h"

#include <memory>

namespace omnidetect {

// Protocol-neutral seam for a future Camera ID SDK. No transport is invented here.
class ICameraIdAdapter {
 public:
  virtual ~ICameraIdAdapter() = default;
  virtual bool connect(const std::string& cameraId, std::string& error) noexcept = 0;
  virtual FrameReadResult receive() noexcept = 0;
  virtual void disconnect() noexcept = 0;
  [[nodiscard]] virtual bool connected() const noexcept = 0;
};

class CameraIdSource final : public IFrameSource {
 public:
  CameraIdSource(std::string cameraId, std::unique_ptr<ICameraIdAdapter> adapter = {});
  ~CameraIdSource() override;
  bool open(std::string& error) noexcept override;
  FrameReadResult read() noexcept override;
  void close() noexcept override;
  [[nodiscard]] bool isOpened() const noexcept override;
  [[nodiscard]] SourceMetadata metadata() const override;

 private:
  std::string cameraId_;
  std::unique_ptr<ICameraIdAdapter> adapter_;
};

}  // namespace omnidetect

