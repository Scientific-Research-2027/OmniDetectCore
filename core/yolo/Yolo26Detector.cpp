#include "core/yolo/Yolo26Detector.h"

#include "core/logging/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <sstream>

namespace omnidetect {
namespace {

using Clock = std::chrono::steady_clock;

struct MatrixView {
  const Tensor* tensor{nullptr};
  std::size_t rows{0};
  std::size_t columns{0};
  bool transposed{false};

  [[nodiscard]] float at(const std::size_t row, const std::size_t column) const {
    return transposed ? tensor->values[column * rows + row]
                      : tensor->values[row * columns + column];
  }
};

bool isFeatureCount(const std::size_t value, const DetectorConfig& config) {
  if (value == 6) return true;
  if (!config.classNames.empty()) {
    return value == 4U + config.classNames.size() || value == 5U + config.classNames.size();
  }
  return value >= 5U && value <= 1024U;
}

bool makeMatrixView(const Tensor& tensor, const DetectorConfig& config, MatrixView& view, std::string& error) {
  if (!tensor.valid()) {
    error = "Backend returned an invalid tensor (shape does not match value count)";
    return false;
  }
  std::size_t first = 0;
  std::size_t second = 0;
  if (tensor.shape.size() == 2) {
    first = static_cast<std::size_t>(tensor.shape[0]);
    second = static_cast<std::size_t>(tensor.shape[1]);
  } else if (tensor.shape.size() == 3 && tensor.shape[0] == 1) {
    first = static_cast<std::size_t>(tensor.shape[1]);
    second = static_cast<std::size_t>(tensor.shape[2]);
  } else {
    error = "YOLO output must have shape [N,F], [1,N,F], or [1,F,N]";
    return false;
  }

  const bool firstLooksLikeFeatures = isFeatureCount(first, config);
  const bool secondLooksLikeFeatures = isFeatureCount(second, config);
  if (secondLooksLikeFeatures && (!firstLooksLikeFeatures || first > second)) {
    view = {&tensor, first, second, false};
  } else if (firstLooksLikeFeatures) {
    view = {&tensor, second, first, true};
  } else {
    std::ostringstream stream;
    stream << "Cannot identify YOLO feature dimension in output shape [" << first << ',' << second
           << "]; provide class_names/output_layout matching the export metadata";
    error = stream.str();
    return false;
  }
  return true;
}

float intersectionOverUnion(const BoundingBox& left, const BoundingBox& right) noexcept {
  const auto x1 = std::max(left.x, right.x);
  const auto y1 = std::max(left.y, right.y);
  const auto x2 = std::min(left.x + left.width, right.x + right.width);
  const auto y2 = std::min(left.y + left.height, right.y + right.height);
  const auto intersection = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
  const auto unionArea = left.width * left.height + right.width * right.height - intersection;
  return unionArea > 0.0F ? intersection / unionArea : 0.0F;
}

std::string className(const DetectorConfig& config, const int classId) {
  if (classId >= 0 && static_cast<std::size_t>(classId) < config.classNames.size()) {
    return config.classNames[static_cast<std::size_t>(classId)];
  }
  return "class_" + std::to_string(classId);
}

}  // namespace

Yolo26Detector::Yolo26Detector(DetectorConfig detectorConfig,
                               std::unique_ptr<IInferenceBackend> backend,
                               BackendConfig backendConfig)
    : config_(std::move(detectorConfig)),
      preprocessor_({config_.inputWidth, config_.inputHeight, config_.inputRgb,
                     config_.normalizationScale, 114.0F}),
      backend_(std::move(backend)) {
  if (!validateConfig(initializationError_)) return;
  if (!backend_) {
    initializationError_ = "No inference backend was provided";
    return;
  }
  if (backendConfig.modelPath.empty()) backendConfig.modelPath = config_.modelPath;
  const auto loadBegin = Clock::now();
  if (!backend_->initialize(backendConfig, initializationError_)) {
    if (initializationError_.empty()) initializationError_ = "Inference backend initialization failed";
    return;
  }
  const auto loadMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - loadBegin).count();
  log::info("YOLO26 detector initialized with backend " + backend_->name() + " in " +
            std::to_string(loadMilliseconds) + " ms");
}

Yolo26Detector::~Yolo26Detector() {
  if (backend_) backend_->shutdown();
}

bool Yolo26Detector::validateConfig(std::string& error) const noexcept {
  if (config_.inputWidth <= 0 || config_.inputHeight <= 0) {
    error = "Detector input dimensions must be positive";
  } else if (config_.confidenceThreshold < 0.0F || config_.confidenceThreshold > 1.0F) {
    error = "Confidence threshold must be in [0,1]";
  } else if (config_.iouThreshold < 0.0F || config_.iouThreshold > 1.0F) {
    error = "IoU threshold must be in [0,1]";
  } else if (config_.maxDetections == 0) {
    error = "maxDetections must be greater than zero";
  } else if (config_.normalizationScale <= 0.0F) {
    error = "Normalization scale must be positive";
  }
  return error.empty();
}

bool Yolo26Detector::isReady() const noexcept {
  return initializationError_.empty() && backend_ && backend_->isReady();
}

DetectionResult Yolo26Detector::detect(const Frame& frame) noexcept {
  DetectionResult result;
  result.frameId = frame.frameId;
  result.sourceId = frame.sourceId;
  const auto begin = Clock::now();
  if (!frame.valid()) {
    result.status = DetectionStatus::InvalidFrame;
    result.error = "Invalid frame";
    return result;
  }
  if (!isReady()) {
    result.status = DetectionStatus::ModelNotReady;
    result.error = initializationError_.empty() ? "Detector is not ready" : initializationError_;
    return result;
  }

  try {
    const auto preprocessBegin = Clock::now();
    auto preprocessed = preprocessor_.process(frame);
    const auto preprocessEnd = Clock::now();
    result.preprocessTime = std::chrono::duration_cast<std::chrono::microseconds>(preprocessEnd - preprocessBegin);
    if (!preprocessed.ok()) {
      result.status = DetectionStatus::InvalidFrame;
      result.error = std::move(preprocessed.error);
      return result;
    }

    auto backendResult = backend_->infer(preprocessed.input);
    const auto inferenceEnd = Clock::now();
    result.inferenceTime = std::chrono::duration_cast<std::chrono::microseconds>(inferenceEnd - preprocessEnd);
    if (!backendResult.success) {
      result.status = DetectionStatus::BackendError;
      result.error = std::move(backendResult.error);
      return result;
    }
    if (backendResult.outputs.empty()) {
      result.status = DetectionStatus::InvalidOutput;
      result.error = "Backend returned no output tensors";
      return result;
    }

    std::string decodeError;
    result.detections = decode(backendResult.outputs.front(), preprocessed.transform, decodeError);
    const auto postprocessEnd = Clock::now();
    result.postprocessTime = std::chrono::duration_cast<std::chrono::microseconds>(postprocessEnd - inferenceEnd);
    if (!decodeError.empty()) {
      result.status = DetectionStatus::InvalidOutput;
      result.error = std::move(decodeError);
    } else {
      result.status = result.detections.empty() ? DetectionStatus::NoDetections : DetectionStatus::Ok;
    }
  } catch (const std::exception& exception) {
    result.status = DetectionStatus::BackendError;
    result.error = exception.what();
  } catch (...) {
    result.status = DetectionStatus::BackendError;
    result.error = "Detection failed with an unknown error";
  }
  result.totalLatency = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin);
  return result;
}

std::vector<Detection> Yolo26Detector::decode(const Tensor& tensor,
                                              const LetterboxTransform& transform,
                                              std::string& error) const {
  MatrixView matrix;
  if (!makeMatrixView(tensor, config_, matrix, error)) return {};

  auto layout = config_.outputLayout;
  if (layout == YoloOutputLayout::Auto) {
    layout = matrix.columns == 6 ? YoloOutputLayout::XyxyScoreClass
                                 : YoloOutputLayout::CenterXywhClassScores;
  }
  if (layout == YoloOutputLayout::XyxyScoreClass && matrix.columns != 6) {
    error = "xyxy_score_class output must have exactly 6 values per detection";
    return {};
  }
  if (layout == YoloOutputLayout::CenterXywhClassScores && matrix.columns < 5) {
    error = "center_xywh_class_scores output must have at least 5 values per candidate";
    return {};
  }

  std::vector<Detection> candidates;
  candidates.reserve(std::min<std::size_t>(matrix.rows, config_.maxDetections * 4U));
  for (std::size_t row = 0; row < matrix.rows; ++row) {
    int classId = -1;
    float confidence = 0.0F;
    BoundingBox modelBox;
    if (layout == YoloOutputLayout::XyxyScoreClass) {
      const auto x1 = matrix.at(row, 0);
      const auto y1 = matrix.at(row, 1);
      const auto x2 = matrix.at(row, 2);
      const auto y2 = matrix.at(row, 3);
      confidence = matrix.at(row, 4);
      classId = static_cast<int>(std::lround(matrix.at(row, 5)));
      modelBox = {x1, y1, x2 - x1, y2 - y1};
    } else {
      const auto configuredClasses = config_.classNames.size();
      const bool hasObjectness = configuredClasses > 0 && matrix.columns == configuredClasses + 5U;
      const std::size_t scoreOffset = hasObjectness ? 5U : 4U;
      if (scoreOffset >= matrix.columns) continue;
      const auto scoreCount = matrix.columns - scoreOffset;
      std::size_t bestClass = 0;
      float bestScore = -std::numeric_limits<float>::infinity();
      for (std::size_t index = 0; index < scoreCount; ++index) {
        if (matrix.at(row, scoreOffset + index) > bestScore) {
          bestScore = matrix.at(row, scoreOffset + index);
          bestClass = index;
        }
      }
      classId = static_cast<int>(bestClass);
      confidence = bestScore * (hasObjectness ? matrix.at(row, 4) : 1.0F);
      const auto centerX = matrix.at(row, 0);
      const auto centerY = matrix.at(row, 1);
      const auto width = matrix.at(row, 2);
      const auto height = matrix.at(row, 3);
      modelBox = {centerX - width * 0.5F, centerY - height * 0.5F, width, height};
    }

    if (!std::isfinite(confidence) || confidence < config_.confidenceThreshold || classId < 0) continue;
    if (!config_.classWhitelist.empty() && !config_.classWhitelist.contains(classId)) continue;
    if (config_.classBlacklist.contains(classId)) continue;
    if (modelBox.width <= 0.0F || modelBox.height <= 0.0F) continue;

    const auto maximumCoordinate = std::max({std::abs(modelBox.x), std::abs(modelBox.y),
                                             std::abs(modelBox.width), std::abs(modelBox.height)});
    if (maximumCoordinate <= 2.0F) {
      modelBox.x *= static_cast<float>(config_.inputWidth);
      modelBox.width *= static_cast<float>(config_.inputWidth);
      modelBox.y *= static_cast<float>(config_.inputHeight);
      modelBox.height *= static_cast<float>(config_.inputHeight);
    }
    auto original = transform.toOriginal(modelBox);
    if (original.width <= 0.0F || original.height <= 0.0F) continue;
    candidates.push_back({classId, className(config_, classId), confidence, original, std::nullopt, 0, false});
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Detection& left, const Detection& right) { return left.confidence > right.confidence; });
  std::vector<Detection> selected;
  selected.reserve(std::min(candidates.size(), config_.maxDetections));
  for (auto& candidate : candidates) {
    bool suppressed = false;
    for (const auto& accepted : selected) {
      if (candidate.classId == accepted.classId &&
          intersectionOverUnion(candidate.bbox, accepted.bbox) > config_.iouThreshold) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) selected.push_back(std::move(candidate));
    if (selected.size() >= config_.maxDetections) break;
  }
  return selected;
}

}  // namespace omnidetect
