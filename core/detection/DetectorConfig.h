#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace omnidetect {

enum class YoloOutputLayout { Auto, XyxyScoreClass, CenterXywhClassScores };

struct DetectorConfig {
  std::filesystem::path modelPath;
  int inputWidth{416};
  int inputHeight{416};
  float confidenceThreshold{0.45F};
  float iouThreshold{0.50F};
  std::size_t maxDetections{100};
  bool inputRgb{true};
  float normalizationScale{1.0F / 255.0F};
  std::vector<std::string> classNames;
  std::unordered_set<int> classWhitelist;
  std::unordered_set<int> classBlacklist;
  YoloOutputLayout outputLayout{YoloOutputLayout::Auto};
};

}  // namespace omnidetect

