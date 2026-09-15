#pragma once

#include "control/ICameraControl.h"

#include <memory>
#include <string>

namespace omnidetect {

class V4l2CameraControl final : public ICameraControl {
 public:
  explicit V4l2CameraControl(std::string devicePath);
  ~V4l2CameraControl() override;

  bool open(std::string& error) noexcept override;
  bool execute(const CameraControlCommand& command, std::string& error) noexcept override;
  void close() noexcept override;
  [[nodiscard]] bool isReady() const noexcept override;
  [[nodiscard]] CameraControlCapabilities capabilities() const noexcept override;
  [[nodiscard]] std::string name() const override { return "v4l2"; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace omnidetect
