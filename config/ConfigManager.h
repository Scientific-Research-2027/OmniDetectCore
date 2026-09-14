#pragma once

#include "core/detection/DetectorConfig.h"
#include "core/inference/IInferenceBackend.h"
#include "core/monitoring/ObjectMonitor.h"
#include "core/tracking/ObjectTracker.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace omnidetect {

enum class SourceType { Image, Webcam, Video, CameraId };

struct SourceConfig {
  SourceType type{SourceType::Webcam};
  std::filesystem::path path;
  int device{0};
  int width{1280};
  int height{720};
  double fps{0.0};
  bool loop{false};
  std::string sourceId{"default"};
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

