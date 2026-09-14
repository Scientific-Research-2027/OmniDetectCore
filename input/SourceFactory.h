#pragma once

#include "config/ConfigManager.h"
#include "input/IFrameSource.h"

#include <memory>
#include <string>

namespace omnidetect {

[[nodiscard]] std::unique_ptr<IFrameSource> createFrameSource(const SourceConfig& config, std::string& error);

}  // namespace omnidetect

