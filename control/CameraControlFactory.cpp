#include "control/CameraControlFactory.h"

#include "control/OnvifCameraControl.h"
#include "control/V4l2CameraControl.h"

namespace omnidetect {

std::unique_ptr<ICameraControl> createCameraControl(const CameraConfig& config, std::string& error) {
  if (!config.control.enabled || config.control.type == CameraControlType::None) {
    error.clear();
    return nullptr;
  }
  if (config.control.type == CameraControlType::Onvif) {
    error.clear();
    return std::make_unique<OnvifCameraControl>(config.control);
  }
  if (config.control.type == CameraControlType::V4l2) {
    auto device = config.control.endpoint;
    if (device.empty()) device = config.source.devicePath;
    error.clear();
    return std::make_unique<V4l2CameraControl>(std::move(device));
  }
  error = "Unknown camera control type";
  return nullptr;
}

}  // namespace omnidetect
