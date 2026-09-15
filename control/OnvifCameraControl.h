#pragma once

#include "config/ConfigManager.h"
#include "control/ICameraControl.h"

#include <atomic>

namespace omnidetect {

class OnvifCameraControl final : public ICameraControl {
 public:
  explicit OnvifCameraControl(CameraControlConfig config);

  bool open(std::string& error) noexcept override;
  bool execute(const CameraControlCommand& command, std::string& error) noexcept override;
  void close() noexcept override;
  [[nodiscard]] bool isReady() const noexcept override { return ready_.load(std::memory_order_acquire); }
  [[nodiscard]] CameraControlCapabilities capabilities() const noexcept override;
  [[nodiscard]] std::string name() const override { return "onvif"; }

 private:
  CameraControlConfig config_;
  std::atomic<bool> ready_{false};
};

}  // namespace omnidetect
