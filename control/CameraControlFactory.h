#pragma once

#include "config/ConfigManager.h"
#include "control/ICameraControl.h"

#include <memory>
#include <string>

namespace omnidetect {

[[nodiscard]] std::unique_ptr<ICameraControl> createCameraControl(const CameraConfig& config,
                                                                  std::string& error);

}  // namespace omnidetect
