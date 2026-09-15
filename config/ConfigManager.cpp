#include "config/ConfigManager.h"
#include "config/MultiCameraConfigParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omnidetect {
namespace {

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  value = value.substr(first, last - first + 1);
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

bool parseBool(const std::string& value, bool& destination) {
  const auto normalized = lower(trim(value));
  if (normalized == "true" || normalized == "yes" || normalized == "on" || normalized == "1") {
    destination = true;
    return true;
  }
  if (normalized == "false" || normalized == "no" || normalized == "off" || normalized == "0") {
    destination = false;
    return true;
  }
  return false;
}

template <typename Number>
bool parseNumber(const std::string& value, Number& destination) {
  std::istringstream stream(trim(value));
  stream >> destination;
  return stream && stream.eof();
}

std::vector<std::string> parseList(std::string value) {
  value = trim(std::move(value));
  if (!value.empty() && value.front() == '[' && value.back() == ']') value = value.substr(1, value.size() - 2);
  std::vector<std::string> output;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    item = trim(std::move(item));
    if (!item.empty()) output.push_back(std::move(item));
  }
  return output;
}

struct FlatYaml {
  std::unordered_map<std::string, std::string> scalars;
  std::unordered_map<std::string, std::vector<std::string>> lists;
};

bool flattenYaml(std::istream& input, FlatYaml& output, std::string& error) {
  std::vector<std::pair<int, std::string>> parents;
  std::string currentList;
  std::string line;
  std::size_t lineNumber = 0;
  bool skipCameraSequence = false;
  while (std::getline(input, line)) {
    ++lineNumber;
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.erase(comment);
    if (trim(line).empty()) continue;
    const auto contentStart = line.find_first_not_of(' ');
    if (contentStart == std::string::npos || contentStart % 2 != 0) {
      error = "YAML indentation must use multiples of two spaces at line " + std::to_string(lineNumber);
      return false;
    }
    const int indent = static_cast<int>(contentStart);
    auto content = trim(line.substr(contentStart));
    if (indent == 0 && content == "cameras:") {
      skipCameraSequence = true;
      continue;
    }
    if (skipCameraSequence && indent > 0) continue;
    if (skipCameraSequence && indent == 0) skipCameraSequence = false;
    if (content.starts_with("- ")) {
      if (currentList.empty()) {
        error = "Sequence item has no parent key at line " + std::to_string(lineNumber);
        return false;
      }
      output.lists[currentList].push_back(trim(content.substr(2)));
      continue;
    }

    while (!parents.empty() && parents.back().first >= indent) parents.pop_back();
    const auto colon = content.find(':');
    if (colon == std::string::npos) {
      error = "Expected key: value at line " + std::to_string(lineNumber);
      return false;
    }
    const auto key = trim(content.substr(0, colon));
    const auto value = trim(content.substr(colon + 1));
    std::string path;
    for (const auto& [unused, parent] : parents) {
      static_cast<void>(unused);
      if (!path.empty()) path += '.';
      path += parent;
    }
    if (!path.empty()) path += '.';
    path += key;

    if (value.empty()) {
      parents.emplace_back(indent, key);
      currentList = path;
    } else if (value.front() == '[') {
      output.lists[path] = parseList(value);
      currentList.clear();
    } else {
      output.scalars[path] = value;
      currentList.clear();
    }
  }
  return true;
}

template <typename T>
bool assignNumber(const FlatYaml& yaml, const std::string& key, T& target, std::string& error) {
  const auto found = yaml.scalars.find(key);
  if (found == yaml.scalars.end()) return true;
  if (!parseNumber(found->second, target)) {
    error = "Invalid numeric value for " + key + ": " + found->second;
    return false;
  }
  return true;
}

bool assignBool(const FlatYaml& yaml, const std::string& key, bool& target, std::string& error) {
  const auto found = yaml.scalars.find(key);
  if (found == yaml.scalars.end()) return true;
  if (!parseBool(found->second, target)) {
    error = "Invalid boolean value for " + key + ": " + found->second;
    return false;
  }
  return true;
}

std::string scalar(const FlatYaml& yaml, const std::string& key, std::string fallback = {}) {
  const auto found = yaml.scalars.find(key);
  return found == yaml.scalars.end() ? fallback : trim(found->second);
}

std::filesystem::path resolvedPath(const std::filesystem::path& base, const std::string& value) {
  if (value.empty()) return {};
  std::filesystem::path path(value);
  return path.is_absolute() ? path.lexically_normal() : (base / path).lexically_normal();
}

}  // namespace

ConfigLoadResult ConfigManager::load(const std::filesystem::path& path) noexcept {
  ConfigLoadResult result;
  try {
    std::ifstream input(path);
    if (!input) {
      result.error = "Cannot open config file: " + path.string();
      return result;
    }
    FlatYaml yaml;
    if (!flattenYaml(input, yaml, result.error)) return result;
    const auto base = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();

    result.config.detector.modelPath = resolvedPath(base, scalar(yaml, "model.path"));
    const auto modelType = lower(scalar(yaml, "model.type", "yolo26"));
    if (modelType != "yolo26" && modelType != "yolo26n") result.error = "Unsupported model.type: " + modelType;
    result.config.backendConfig.modelPath = result.config.detector.modelPath;
    result.config.backend = lower(scalar(yaml, "inference.backend", result.config.backend));
    result.config.backendConfig.inputName = scalar(yaml, "inference.input_name", "images");
    result.config.backendConfig.weightsPath = resolvedPath(
        base, scalar(yaml, "model.weights_path", scalar(yaml, "inference.weights_path")));
    if (const auto found = yaml.lists.find("inference.output_names"); found != yaml.lists.end()) {
      result.config.backendConfig.outputNames = found->second;
    }
    assignNumber(yaml, "model.input_size", result.config.detector.inputWidth, result.error);
    result.config.detector.inputHeight = result.config.detector.inputWidth;
    assignNumber(yaml, "model.input_width", result.config.detector.inputWidth, result.error);
    assignNumber(yaml, "model.input_height", result.config.detector.inputHeight, result.error);
    assignNumber(yaml, "model.confidence", result.config.detector.confidenceThreshold, result.error);
    assignNumber(yaml, "model.iou", result.config.detector.iouThreshold, result.error);
    assignNumber(yaml, "model.max_detections", result.config.detector.maxDetections, result.error);
    assignNumber(yaml, "inference.threads", result.config.backendConfig.threadCount, result.error);
    assignNumber(yaml, "inference.worker_count", result.config.sharedInference.workerCount, result.error);
    result.config.sharedInference.scheduler = lower(
        scalar(yaml, "inference.scheduler", result.config.sharedInference.scheduler));
    long long maxFrameAgeMilliseconds = result.config.sharedInference.maxFrameAge.count();
    assignNumber(yaml, "inference.max_frame_age_ms", maxFrameAgeMilliseconds, result.error);
    result.config.sharedInference.maxFrameAge = std::chrono::milliseconds(maxFrameAgeMilliseconds);
    assignNumber(yaml, "inference.max_total_fps", result.config.sharedInference.maximumTotalFps, result.error);
    assignBool(yaml, "application.fail_fast", result.config.failFast, result.error);
    assignBool(yaml, "inference.vulkan", result.config.backendConfig.useVulkan, result.error);
    const auto layout = lower(scalar(yaml, "model.output_layout", "auto"));
    if (layout == "xyxy_score_class") result.config.detector.outputLayout = YoloOutputLayout::XyxyScoreClass;
    else if (layout == "center_xywh_class_scores") result.config.detector.outputLayout = YoloOutputLayout::CenterXywhClassScores;
    else if (layout != "auto") result.error = "Unsupported model.output_layout: " + layout;
    if (const auto found = yaml.lists.find("model.class_names"); found != yaml.lists.end()) {
      result.config.detector.classNames = found->second;
    } else if (const auto classes = yaml.lists.find("model.classes"); classes != yaml.lists.end()) {
      result.config.detector.classNames = classes->second;
    }
    if (const auto found = yaml.lists.find("model.class_whitelist"); found != yaml.lists.end()) {
      for (const auto& value : found->second) result.config.detector.classWhitelist.insert(std::stoi(value));
    }
    if (const auto found = yaml.lists.find("model.class_blacklist"); found != yaml.lists.end()) {
      for (const auto& value : found->second) result.config.detector.classBlacklist.insert(std::stoi(value));
    }

    const auto sourceType = lower(scalar(yaml, "source.type", "webcam"));
    if (sourceType == "image") result.config.source.type = SourceType::Image;
    else if (sourceType == "webcam") result.config.source.type = SourceType::Webcam;
    else if (sourceType == "video") result.config.source.type = SourceType::Video;
    else if (sourceType == "camera_id") result.config.source.type = SourceType::CameraId;
    else if (sourceType == "rtsp") result.config.source.type = SourceType::Rtsp;
    else if (sourceType == "libcamera") result.config.source.type = SourceType::Libcamera;
    else result.error = "Unsupported source.type: " + sourceType;
    result.config.source.path = resolvedPath(base, scalar(yaml, "source.path"));
    result.config.source.uri = scalar(yaml, "source.uri");
    result.config.source.sourceId = scalar(yaml, "source.id", result.config.source.sourceId);
    const auto sourceDevice = scalar(yaml, "source.device");
    if (!sourceDevice.empty() && !parseNumber(sourceDevice, result.config.source.device)) {
      result.config.source.devicePath = sourceDevice;
    }
    assignNumber(yaml, "source.width", result.config.source.width, result.error);
    assignNumber(yaml, "source.height", result.config.source.height, result.error);
    assignNumber(yaml, "source.fps", result.config.source.fps, result.error);
    result.config.source.captureFps = result.config.source.fps;
    assignBool(yaml, "source.loop", result.config.source.loop, result.error);

    assignNumber(yaml, "pipeline.frame_queue_size", result.config.pipeline.frameQueueSize, result.error);
    assignNumber(yaml, "pipeline.result_queue_size", result.config.pipeline.resultQueueSize, result.error);
    assignBool(yaml, "pipeline.drop_old_frames", result.config.pipeline.dropOldFrames, result.error);
    assignBool(yaml, "tracking.enabled", result.config.pipeline.trackingEnabled, result.error);
    assignNumber(yaml, "tracking.minimum_iou", result.config.tracker.minimumIou, result.error);
    assignNumber(yaml, "tracking.max_lost_frames", result.config.tracker.maxLostFrames, result.error);
    assignBool(yaml, "monitoring.enabled", result.config.pipeline.monitoringEnabled, result.error);
    assignNumber(yaml, "monitoring.confirmation_frames", result.config.monitor.confirmationFrames, result.error);
    assignNumber(yaml, "monitoring.lost_frames", result.config.monitor.lostFrames, result.error);
    assignNumber(yaml, "monitoring.minimum_confidence", result.config.monitor.minimumConfidence, result.error);
    if (const auto found = yaml.lists.find("monitoring.target_classes"); found != yaml.lists.end()) {
      result.config.monitor.targetClassNames.insert(found->second.begin(), found->second.end());
    }

    assignBool(yaml, "output.window", result.config.output.window, result.error);
    assignBool(yaml, "output.recorder", result.config.output.recorder, result.error);
    assignBool(yaml, "output.mqtt", result.config.output.mqtt, result.error);
    result.config.output.imagePath = resolvedPath(base, scalar(yaml, "output.image_path"));
    result.config.output.recordingPath = resolvedPath(base, scalar(yaml, "output.recording_path"));
    result.config.logLevel = lower(scalar(yaml, "logging.level", "info"));

    if (!result.error.empty()) return result;
    if (!detail::parseMultiCameraConfig(path, base, result.config, result.error)) return result;
    static_cast<void>(validate(result.config, result.error));
  } catch (const std::exception& exception) {
    result.error = exception.what();
  } catch (...) {
    result.error = "Config loading failed with an unknown error";
  }
  return result;
}

bool ConfigManager::validate(const AppConfig& config, std::string& error) noexcept {
  std::error_code filesystemError;
  if (config.detector.inputWidth <= 0 || config.detector.inputHeight <= 0) {
    error = "model input dimensions must be positive";
  } else if (config.detector.confidenceThreshold < 0.0F || config.detector.confidenceThreshold > 1.0F) {
    error = "model.confidence must be in [0,1]";
  } else if (config.detector.iouThreshold < 0.0F || config.detector.iouThreshold > 1.0F) {
    error = "model.iou must be in [0,1]";
  } else if (config.backend.empty()) {
    error = "inference.backend must not be empty";
  } else if (config.backendConfig.threadCount <= 0) {
    error = "inference.threads must be greater than zero";
  } else if (config.detector.modelPath.empty()) {
    error = "model.path must not be empty";
  } else if (!std::filesystem::exists(config.detector.modelPath, filesystemError)) {
    error = "model.path does not exist: " + config.detector.modelPath.string();
  } else if (config.sharedInference.workerCount != 1) {
    error = "inference.worker_count must be 1 until a backend is explicitly proven thread-safe";
  } else if (config.sharedInference.scheduler != "round_robin") {
    error = "inference.scheduler currently supports only round_robin";
  } else if (config.sharedInference.maxFrameAge.count() <= 0) {
    error = "inference.max_frame_age_ms must be greater than zero";
  } else if (!std::isfinite(config.sharedInference.maximumTotalFps) ||
             config.sharedInference.maximumTotalFps < 0.0) {
    error = "inference.max_total_fps must not be negative";
  } else if (config.pipeline.frameQueueSize == 0 || config.pipeline.frameQueueSize > 64) {
    error = "pipeline.frame_queue_size must be in [1,64]";
  } else if (config.pipeline.resultQueueSize == 0 || config.pipeline.resultQueueSize > 64) {
    error = "pipeline.result_queue_size must be in [1,64]";
  } else if (config.cameras.empty() && (config.source.width <= 0 || config.source.height <= 0)) {
    error = "source dimensions must be positive";
  } else if (config.cameras.empty() && config.source.type == SourceType::Webcam && config.source.device < 0) {
    error = "source.device must not be negative";
  } else if (config.cameras.empty() &&
             (config.source.type == SourceType::Image || config.source.type == SourceType::Video) &&
             config.source.path.empty()) {
    error = "source.path is required for image/video sources";
  } else if (config.cameras.empty() &&
             (config.source.type == SourceType::Image || config.source.type == SourceType::Video) &&
             !std::filesystem::exists(config.source.path, filesystemError)) {
    error = "source.path does not exist: " + config.source.path.string();
  } else if (config.cameras.empty() && config.source.type == SourceType::Rtsp &&
             (!config.source.uri.starts_with("rtsp://") && !config.source.uri.starts_with("rtsps://"))) {
    error = "source.uri must begin with rtsp:// or rtsps://";
  } else if (config.monitor.confirmationFrames == 0 || config.monitor.lostFrames == 0) {
    error = "monitoring frame thresholds must be greater than zero";
  } else if (config.logLevel != "trace" && config.logLevel != "debug" && config.logLevel != "info" &&
             config.logLevel != "warning" && config.logLevel != "error" && config.logLevel != "critical") {
    error = "logging.level must be trace, debug, info, warning, error, or critical";
  }
  if (!error.empty()) return false;

  std::unordered_set<std::string> cameraIds;
  for (const auto& camera : config.cameras) {
    if (camera.id.empty()) {
      error = "camera.id must not be empty";
    } else if (!cameraIds.insert(camera.id).second) {
      error = "Duplicate camera id: " + camera.id;
    } else if (camera.priority <= 0 || camera.priority > 16) {
      error = "camera.priority must be in [1,16] for " + camera.id;
    } else if (camera.frameQueueSize == 0 || camera.frameQueueSize > 8) {
      error = "camera source.queue_size must be in [1,8] for " + camera.id;
    } else if (camera.source.width <= 0 || camera.source.height <= 0) {
      error = "camera source dimensions must be positive for " + camera.id;
    } else if (!std::isfinite(camera.source.captureFps) || !std::isfinite(camera.source.inferenceFps) ||
               camera.source.captureFps < 0.0 || camera.source.inferenceFps <= 0.0) {
      error = "camera capture_fps must be non-negative and inference_fps must be positive for " + camera.id;
    } else if (camera.startupTimeout.count() <= 0 || camera.reconnectInitialDelay.count() <= 0 ||
               camera.reconnectMaximumDelay < camera.reconnectInitialDelay) {
      error = "camera startup/reconnect delays are invalid for " + camera.id;
    } else if ((camera.source.type == SourceType::Image || camera.source.type == SourceType::Video) &&
               camera.source.path.empty()) {
      error = "camera source.path is required for " + camera.id;
    } else if ((camera.source.type == SourceType::Image || camera.source.type == SourceType::Video) &&
               !std::filesystem::exists(camera.source.path, filesystemError)) {
      error = "camera source.path does not exist for " + camera.id;
    } else if (camera.source.type == SourceType::Rtsp &&
               (!camera.source.uri.starts_with("rtsp://") && !camera.source.uri.starts_with("rtsps://"))) {
      error = "camera source.uri must begin with rtsp:// or rtsps:// for " + camera.id;
    } else if (camera.tracker.minimumIou < 0.0F || camera.tracker.minimumIou > 1.0F ||
               camera.tracker.minimumHits == 0) {
      error = "camera tracking settings are invalid for " + camera.id;
    } else if (camera.monitor.confirmationFrames == 0 || camera.monitor.lostFrames == 0 ||
               camera.monitor.minimumConfidence < 0.0F || camera.monitor.minimumConfidence > 1.0F) {
      error = "camera monitoring settings are invalid for " + camera.id;
    } else if (camera.control.enabled && camera.control.type == CameraControlType::None) {
      error = "camera control.type is required when control is enabled for " + camera.id;
    } else if (camera.control.enabled && camera.control.type == CameraControlType::Onvif &&
               camera.control.endpoint.empty()) {
      error = "camera control.endpoint is required for ONVIF camera " + camera.id;
    } else if (camera.control.enabled && camera.control.type == CameraControlType::V4l2 &&
               camera.control.endpoint.empty() && camera.source.devicePath.empty()) {
      error = "camera control.endpoint or source.device path is required for V4L2 camera " + camera.id;
    } else if (camera.control.queueSize == 0 || camera.control.queueSize > 32 ||
               camera.control.timeout.count() <= 0 || camera.control.maximumCommandsPerSecond <= 0.0) {
      error = "camera control resource limits are invalid for " + camera.id;
    } else if (camera.control.enabled &&
               (camera.control.manualOverrideHold.count() < 0 || camera.control.profileToken.empty())) {
      error = "camera control profile/manual override is invalid for " + camera.id;
    } else if (camera.control.autoPtz.enabled && !camera.control.enabled) {
      error = "camera auto_ptz requires camera control to be enabled for " + camera.id;
    } else if (camera.control.autoPtz.deadZone < 0.0F || camera.control.autoPtz.deadZone >= 1.0F ||
               camera.control.autoPtz.hysteresis < 0.0F ||
               camera.control.autoPtz.hysteresis > camera.control.autoPtz.deadZone ||
               camera.control.autoPtz.panTiltGain <= 0.0F ||
               camera.control.autoPtz.maximumSpeed <= 0.0F || camera.control.autoPtz.maximumSpeed > 1.0F ||
               camera.control.autoPtz.targetBoxRatio <= 0.0F || camera.control.autoPtz.targetBoxRatio > 1.0F ||
               camera.control.autoPtz.zoomDeadBand < 0.0F || camera.control.autoPtz.zoomDeadBand >= 1.0F ||
               camera.control.autoPtz.commandInterval.count() <= 0 ||
               camera.control.autoPtz.targetHold.count() < 0 ||
               camera.control.autoPtz.lostTimeout.count() <= 0 || camera.control.autoPtz.switchMargin < 0.0F) {
      error = "camera auto_ptz settings are invalid for " + camera.id;
    }
    if (!error.empty()) return false;
  }
  return error.empty();
}

}  // namespace omnidetect
