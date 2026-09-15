#pragma once

#include "config/ConfigManager.h"

#include <filesystem>
#include <string>

namespace omnidetect::detail {

bool parseMultiCameraConfig(const std::filesystem::path& path,
                            const std::filesystem::path& base,
                            AppConfig& config,
                            std::string& error) noexcept;

}  // namespace omnidetect::detail

