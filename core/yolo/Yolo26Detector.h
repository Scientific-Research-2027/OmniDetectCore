#pragma once

#include "core/detection/DetectorConfig.h"
#include "core/detection/IObjectDetector.h"
#include "core/inference/IInferenceBackend.h"
#include "core/preprocessing/Preprocessor.h"

#include <memory>
#include <string>

namespace omnidetect {

class Yolo26Detector final : public IObjectDetector {
 public:
  Yolo26Detector(DetectorConfig detectorConfig,
                 std::unique_ptr<IInferenceBackend> backend,
                 BackendConfig backendConfig);
  ~Yolo26Detector() override;

  Yolo26Detector(const Yolo26Detector&) = delete;
  Yolo26Detector& operator=(const Yolo26Detector&) = delete;

  [[nodiscard]] bool isReady() const noexcept override;
  [[nodiscard]] std::string name() const override { return "YOLO26"; }
  [[nodiscard]] const std::string& initializationError() const noexcept { return initializationError_; }
  DetectionResult detect(const Frame& frame) noexcept override;

 private:
  DetectorConfig config_;
  Preprocessor preprocessor_;
  std::unique_ptr<IInferenceBackend> backend_;
  std::string initializationError_;

  [[nodiscard]] bool validateConfig(std::string& error) const noexcept;
  [[nodiscard]] std::vector<Detection> decode(const Tensor& tensor,
                                              const LetterboxTransform& transform,
                                              std::string& error) const;
};

}  // namespace omnidetect

