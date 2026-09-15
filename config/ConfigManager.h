#pragma once

#include "core/detection/DetectorConfig.h"
#include "core/inference/IInferenceBackend.h"
#include "core/monitoring/ObjectMonitor.h"
#include "core/tracking/ObjectTracker.h"

#include <cstddef>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

namespace omnidetect {

enum class SourceType { Image, Webcam, Video, CameraId, Rtsp, Libcamera };

struct SourceConfig {
  SourceType type{SourceType::Webcam};
  std::filesystem::path path;
  std::string uri;
  std::string devicePath;
  int device{0};
  int width{1280};
  int height{720};
  double fps{0.0};
  double captureFps{0.0};
  double inferenceFps{3.0};
  bool loop{false};
  std::string sourceId{"default"};
};

enum class CameraControlType { None, Onvif, V4l2 };

struct AutoPtzConfig {
  bool enabled{false};
  float deadZone{0.10F};
  float hysteresis{0.02F};
  float panTiltGain{0.50F};
  float maximumSpeed{0.50F};
  float targetBoxRatio{0.20F};
  float zoomDeadBand{0.05F};
  std::chrono::milliseconds commandInterval{250};
  std::chrono::milliseconds targetHold{2000};
  std::chrono::milliseconds lostTimeout{1000};
  float switchMargin{0.15F};
  std::vector<int> priorityClasses;
};

struct CameraControlConfig {
  bool enabled{false};
  CameraControlType type{CameraControlType::None};
  std::string endpoint;
  std::string profileToken{"Profile_1"};
  std::string username;
  std::string password;
  std::chrono::milliseconds timeout{2000};
  std::chrono::milliseconds manualOverrideHold{3000};
  std::size_t queueSize{4};
  double maximumCommandsPerSecond{4.0};
  AutoPtzConfig autoPtz;
};

struct CameraConfig {
  std::string id;
  bool enabled{true};
  int priority{1};
  SourceConfig source;
  std::size_t frameQueueSize{1};
  std::chrono::milliseconds startupTimeout{5000};
  std::chrono::milliseconds reconnectInitialDelay{250};
  std::chrono::milliseconds reconnectMaximumDelay{10000};
  bool trackingEnabled{true};
  TrackerConfig tracker;
  bool monitoringEnabled{true};
  MonitorConfig monitor;
  CameraControlConfig control;
};

struct SharedInferenceConfig {
  std::size_t workerCount{1};
  std::string scheduler{"round_robin"};
  std::chrono::milliseconds maxFrameAge{500};
  double maximumTotalFps{0.0};
};

struct PipelineConfig {
  std::size_t frameQueueSize{2};
  std::size_t resultQueueSize{2};
  bool dropOldFrames{true};
  bool trackingEnabled{true};
  bool monitoringEnabled{true};
};

struct OutputConfig {
  bool window{true};
  bool recorder{false};
  bool mqtt{false};
  std::filesystem::path imagePath;
  std::filesystem::path recordingPath;
};

struct AppConfig {
  DetectorConfig detector;
  std::string backend{"ncnn"};
  BackendConfig backendConfig;
  SourceConfig source;
  std::vector<CameraConfig> cameras;
  SharedInferenceConfig sharedInference;
  bool failFast{false};
  PipelineConfig pipeline;
  TrackerConfig tracker;
  MonitorConfig monitor;
  OutputConfig output;
  std::string logLevel{"info"};
};

struct ConfigLoadResult {
  AppConfig config;
  std::string error;
  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

class ConfigManager {
 public:
  [[nodiscard]] static ConfigLoadResult load(const std::filesystem::path& path) noexcept;
  [[nodiscard]] static bool validate(const AppConfig& config, std::string& error) noexcept;
};

}  // namespace omnidetect
