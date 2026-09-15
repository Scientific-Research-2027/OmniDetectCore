#include "config/MultiCameraConfigParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace omnidetect::detail {
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

std::string withoutComment(const std::string& line) {
  bool singleQuoted = false;
  bool doubleQuoted = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    if (line[index] == '\'' && !doubleQuoted) singleQuoted = !singleQuoted;
    else if (line[index] == '"' && !singleQuoted) doubleQuoted = !doubleQuoted;
    else if (line[index] == '#' && !singleQuoted && !doubleQuoted &&
             (index == 0 || std::isspace(static_cast<unsigned char>(line[index - 1])) != 0)) {
      return line.substr(0, index);
    }
  }
  return line;
}

bool split(const std::string& content, std::string& key, std::string& value) {
  const auto colon = content.find(':');
  if (colon == std::string::npos) return false;
  key = lower(trim(content.substr(0, colon)));
  value = trim(content.substr(colon + 1));
  return !key.empty();
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
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    item = trim(std::move(item));
    if (!item.empty()) result.push_back(std::move(item));
  }
  return result;
}

bool readEnvironment(const std::string& name, std::string& value) {
#ifdef _WIN32
  char* buffer = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&buffer, &length, name.c_str()) != 0 || buffer == nullptr) return false;
  value.assign(buffer, length > 0 ? length - 1U : 0U);
  std::free(buffer);
  return true;
#else
  const char* buffer = std::getenv(name.c_str());
  if (buffer == nullptr) return false;
  value = buffer;
  return true;
#endif
}

bool expandEnvironment(std::string& value, std::string& error) {
  std::size_t offset = 0;
  while ((offset = value.find("${", offset)) != std::string::npos) {
    const auto end = value.find('}', offset + 2);
    if (end == std::string::npos) {
      error = "Environment placeholder is missing a closing brace";
      return false;
    }
    const auto name = value.substr(offset + 2, end - offset - 2);
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](const unsigned char character) {
          return std::isalnum(character) != 0 || character == '_';
        })) {
      error = "Invalid environment variable name in configuration";
      return false;
    }
    std::string replacement;
    if (!readEnvironment(name, replacement)) {
      error = "Required environment variable is not set: " + name;
      return false;
    }
    value.replace(offset, end - offset + 1, replacement);
    offset += replacement.size();
  }
  return true;
}

bool parseSourceType(const std::string& value, SourceType& type) {
  const auto normalized = lower(value);
  if (normalized == "image") type = SourceType::Image;
  else if (normalized == "webcam" || normalized == "v4l2") type = SourceType::Webcam;
  else if (normalized == "video") type = SourceType::Video;
  else if (normalized == "camera_id") type = SourceType::CameraId;
  else if (normalized == "rtsp") type = SourceType::Rtsp;
  else if (normalized == "libcamera" || normalized == "csi") type = SourceType::Libcamera;
  else return false;
  return true;
}

bool assignSource(CameraConfig& camera, const std::string& key, std::string value,
                  const std::filesystem::path& base, std::string& error) {
  if (!expandEnvironment(value, error)) return false;
  auto& source = camera.source;
  if (key == "type") {
    if (!parseSourceType(value, source.type)) error = "Unsupported camera source.type: " + value;
  } else if (key == "uri") {
    source.uri = std::move(value);
  } else if (key == "path") {
    std::filesystem::path parsed(value);
    source.path = parsed.is_absolute() ? parsed.lexically_normal() : (base / parsed).lexically_normal();
  } else if (key == "device") {
    if (!parseNumber(value, source.device)) source.devicePath = std::move(value);
  } else if (key == "width") {
    if (!parseNumber(value, source.width)) error = "Invalid camera source.width";
  } else if (key == "height") {
    if (!parseNumber(value, source.height)) error = "Invalid camera source.height";
  } else if (key == "capture_fps") {
    if (!parseNumber(value, source.captureFps)) error = "Invalid camera source.capture_fps";
    else source.fps = source.captureFps;
  } else if (key == "inference_fps") {
    if (!parseNumber(value, source.inferenceFps)) error = "Invalid camera source.inference_fps";
  } else if (key == "loop") {
    if (!parseBool(value, source.loop)) error = "Invalid camera source.loop";
  } else if (key == "queue_size") {
    if (!parseNumber(value, camera.frameQueueSize)) error = "Invalid camera source.queue_size";
  } else if (key == "startup_timeout_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid startup_timeout_ms";
    else camera.startupTimeout = std::chrono::milliseconds(milliseconds);
  } else if (key == "reconnect_initial_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid reconnect_initial_ms";
    else camera.reconnectInitialDelay = std::chrono::milliseconds(milliseconds);
  } else if (key == "reconnect_max_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid reconnect_max_ms";
    else camera.reconnectMaximumDelay = std::chrono::milliseconds(milliseconds);
  }
  return error.empty();
}

bool assignTracking(CameraConfig& camera, const std::string& key, const std::string& value, std::string& error) {
  if (key == "enabled") {
    if (!parseBool(value, camera.trackingEnabled)) error = "Invalid tracking.enabled";
  } else if (key == "minimum_iou") {
    if (!parseNumber(value, camera.tracker.minimumIou)) error = "Invalid tracking.minimum_iou";
  } else if (key == "max_lost_frames") {
    if (!parseNumber(value, camera.tracker.maxLostFrames)) error = "Invalid tracking.max_lost_frames";
  } else if (key == "minimum_hits") {
    if (!parseNumber(value, camera.tracker.minimumHits)) error = "Invalid tracking.minimum_hits";
  }
  return error.empty();
}

bool assignMonitoring(CameraConfig& camera, const std::string& key, const std::string& value, std::string& error) {
  if (key == "enabled") {
    if (!parseBool(value, camera.monitoringEnabled)) error = "Invalid monitoring.enabled";
  } else if (key == "confirmation_frames") {
    if (!parseNumber(value, camera.monitor.confirmationFrames)) error = "Invalid monitoring.confirmation_frames";
  } else if (key == "lost_frames") {
    if (!parseNumber(value, camera.monitor.lostFrames)) error = "Invalid monitoring.lost_frames";
  } else if (key == "minimum_confidence") {
    if (!parseNumber(value, camera.monitor.minimumConfidence)) error = "Invalid monitoring.minimum_confidence";
  } else if (key == "target_classes") {
    const auto classes = parseList(value);
    camera.monitor.targetClassNames.insert(classes.begin(), classes.end());
  } else if (key == "target_class_ids") {
    for (const auto& item : parseList(value)) {
      int classId = -1;
      if (!parseNumber(item, classId)) error = "Invalid monitoring.target_class_ids";
      else camera.monitor.targetClassIds.insert(classId);
    }
  }
  return error.empty();
}

bool assignControl(CameraConfig& camera, const std::string& key, std::string value, std::string& error) {
  if (!expandEnvironment(value, error)) return false;
  auto& control = camera.control;
  if (key == "enabled") {
    if (!parseBool(value, control.enabled)) error = "Invalid control.enabled";
  } else if (key == "type") {
    const auto normalized = lower(value);
    if (normalized == "none") control.type = CameraControlType::None;
    else if (normalized == "onvif") control.type = CameraControlType::Onvif;
    else if (normalized == "v4l2") control.type = CameraControlType::V4l2;
    else error = "Unsupported camera control.type: " + value;
  } else if (key == "endpoint") control.endpoint = std::move(value);
  else if (key == "profile_token") control.profileToken = std::move(value);
  else if (key == "username") control.username = std::move(value);
  else if (key == "password") control.password = std::move(value);
  else if (key == "timeout_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid control.timeout_ms";
    else control.timeout = std::chrono::milliseconds(milliseconds);
  } else if (key == "manual_override_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid control.manual_override_ms";
    else control.manualOverrideHold = std::chrono::milliseconds(milliseconds);
  } else if (key == "queue_size") {
    if (!parseNumber(value, control.queueSize)) error = "Invalid control.queue_size";
  } else if (key == "max_commands_per_second") {
    if (!parseNumber(value, control.maximumCommandsPerSecond)) error = "Invalid control.max_commands_per_second";
  }
  return error.empty();
}

bool assignAutoPtz(CameraConfig& camera, const std::string& key, const std::string& value, std::string& error) {
  auto& config = camera.control.autoPtz;
  if (key == "enabled") {
    if (!parseBool(value, config.enabled)) error = "Invalid auto_ptz.enabled";
  } else if (key == "dead_zone") {
    if (!parseNumber(value, config.deadZone)) error = "Invalid auto_ptz.dead_zone";
  } else if (key == "hysteresis") {
    if (!parseNumber(value, config.hysteresis)) error = "Invalid auto_ptz.hysteresis";
  } else if (key == "gain") {
    if (!parseNumber(value, config.panTiltGain)) error = "Invalid auto_ptz.gain";
  } else if (key == "max_speed") {
    if (!parseNumber(value, config.maximumSpeed)) error = "Invalid auto_ptz.max_speed";
  } else if (key == "target_box_ratio") {
    if (!parseNumber(value, config.targetBoxRatio)) error = "Invalid auto_ptz.target_box_ratio";
  } else if (key == "zoom_dead_band") {
    if (!parseNumber(value, config.zoomDeadBand)) error = "Invalid auto_ptz.zoom_dead_band";
  } else if (key == "command_interval_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid auto_ptz.command_interval_ms";
    else config.commandInterval = std::chrono::milliseconds(milliseconds);
  } else if (key == "target_hold_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid auto_ptz.target_hold_ms";
    else config.targetHold = std::chrono::milliseconds(milliseconds);
  } else if (key == "lost_timeout_ms") {
    long long milliseconds = 0;
    if (!parseNumber(value, milliseconds)) error = "Invalid auto_ptz.lost_timeout_ms";
    else config.lostTimeout = std::chrono::milliseconds(milliseconds);
  } else if (key == "switch_margin") {
    if (!parseNumber(value, config.switchMargin)) error = "Invalid auto_ptz.switch_margin";
  } else if (key == "priority_classes") {
    for (const auto& item : parseList(value)) {
      int classId = -1;
      if (!parseNumber(item, classId)) error = "Invalid auto_ptz.priority_classes";
      else config.priorityClasses.push_back(classId);
    }
  }
  return error.empty();
}

enum class Section { None, Source, Tracking, Monitoring, Control, AutoPtz };

}  // namespace

bool parseMultiCameraConfig(const std::filesystem::path& path,
                            const std::filesystem::path& base,
                            AppConfig& config,
                            std::string& error) noexcept {
  try {
    std::ifstream input(path);
    if (!input) {
      error = "Cannot open config file: " + path.string();
      return false;
    }
    std::string line;
    bool insideCameras = false;
    CameraConfig* camera = nullptr;
    Section section = Section::None;
    std::string pendingList;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
      ++lineNumber;
      line = withoutComment(line);
      const auto start = line.find_first_not_of(' ');
      if (start == std::string::npos) continue;
      if (start % 2 != 0) {
        error = "YAML indentation must use multiples of two spaces at line " + std::to_string(lineNumber);
        return false;
      }
      const int indent = static_cast<int>(start);
      auto content = trim(line.substr(start));
      if (!insideCameras) {
        if (indent == 0 && content == "cameras:") insideCameras = true;
        continue;
      }
      if (indent == 0) break;
      if (indent == 2 && content.starts_with('-')) {
        config.cameras.emplace_back();
        camera = &config.cameras.back();
        section = Section::None;
        pendingList.clear();
        content = trim(content.substr(1));
        if (content.empty()) continue;
        std::string key;
        std::string value;
        if (!split(content, key, value) || key != "id") {
          error = "Each camera sequence item must begin with id at line " + std::to_string(lineNumber);
          return false;
        }
        camera->id = value;
        continue;
      }
      if (camera == nullptr) {
        error = "Camera property appears before a camera id at line " + std::to_string(lineNumber);
        return false;
      }
      if (content.starts_with("- ") && !pendingList.empty()) {
        const auto value = trim(content.substr(2));
        if (pendingList == "target_classes") camera->monitor.targetClassNames.insert(value);
        else if (pendingList == "target_class_ids") {
          int classId = -1;
          if (!parseNumber(value, classId)) error = "Invalid target class id at line " + std::to_string(lineNumber);
          else camera->monitor.targetClassIds.insert(classId);
        } else if (pendingList == "priority_classes") {
          int classId = -1;
          if (!parseNumber(value, classId)) error = "Invalid priority class id at line " + std::to_string(lineNumber);
          else camera->control.autoPtz.priorityClasses.push_back(classId);
        }
        if (!error.empty()) return false;
        continue;
      }
      std::string key;
      std::string value;
      if (!split(content, key, value)) {
        error = "Expected key: value at line " + std::to_string(lineNumber);
        return false;
      }
      if (indent == 4) {
        pendingList.clear();
        if (value.empty()) {
          if (key == "source") section = Section::Source;
          else if (key == "tracking") section = Section::Tracking;
          else if (key == "monitoring") section = Section::Monitoring;
          else if (key == "control") section = Section::Control;
          else section = Section::None;
        } else if (key == "id") camera->id = value;
        else if (key == "enabled" && !parseBool(value, camera->enabled)) error = "Invalid camera.enabled";
        else if (key == "priority" && !parseNumber(value, camera->priority)) error = "Invalid camera.priority";
      } else if (indent == 6) {
        if (section == Section::AutoPtz) section = Section::Control;
        if (section == Section::Control && key == "auto_ptz" && value.empty()) {
          section = Section::AutoPtz;
          pendingList.clear();
        } else if (value.empty() && (key == "target_classes" || key == "target_class_ids")) {
          pendingList = key;
        } else if (section == Section::Source) assignSource(*camera, key, value, base, error);
        else if (section == Section::Tracking) assignTracking(*camera, key, value, error);
        else if (section == Section::Monitoring) assignMonitoring(*camera, key, value, error);
        else if (section == Section::Control) assignControl(*camera, key, value, error);
      } else if (indent == 8 && section == Section::AutoPtz) {
        if (value.empty() && key == "priority_classes") pendingList = key;
        else assignAutoPtz(*camera, key, value, error);
      }
      if (!error.empty()) {
        error += " at line " + std::to_string(lineNumber);
        return false;
      }
    }
    for (auto& item : config.cameras) {
      item.source.sourceId = item.id;
      if (item.source.captureFps <= 0.0) item.source.captureFps = item.source.fps;
    }
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  } catch (...) {
    error = "Multi-camera configuration parsing failed with an unknown error";
    return false;
  }
}

}  // namespace omnidetect::detail
