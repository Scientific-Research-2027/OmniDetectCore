#pragma once

#include "core/detection/Detection.h"
#include "core/frame/Frame.h"
#include "core/inference/Tensor.h"

#include <string>

namespace omnidetect {

struct PreprocessConfig {
  int targetWidth{416};
  int targetHeight{416};
  bool outputRgb{true};
  float normalizationScale{1.0F / 255.0F};
  float padValue{114.0F};
};

struct LetterboxTransform {
  int originalWidth{0};
  int originalHeight{0};
  int targetWidth{0};
  int targetHeight{0};
  float scale{1.0F};
  float padX{0.0F};
  float padY{0.0F};

  [[nodiscard]] BoundingBox toOriginal(const BoundingBox& modelBox) const noexcept;
};

struct PreprocessedFrame {
  Tensor input;
  LetterboxTransform transform;
  std::string error;
  [[nodiscard]] bool ok() const noexcept { return error.empty() && input.valid(); }
};

class Preprocessor {
 public:
  explicit Preprocessor(PreprocessConfig config);
  [[nodiscard]] const PreprocessConfig& config() const noexcept { return config_; }
  [[nodiscard]] PreprocessedFrame process(const Frame& frame) const noexcept;

 private:
  PreprocessConfig config_;
};

}  // namespace omnidetect

