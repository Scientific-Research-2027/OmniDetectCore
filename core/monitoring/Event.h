#pragma once

#include "core/detection/Detection.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace omnidetect {

enum class EventType {
  ObjectDetected,
  ObjectConfirmed,
  ObjectLost,
  SourceDisconnected,
  SourceRecovered,
  CameraConnected,
  CameraDisconnected,
  CameraRecovered,
  CameraControlError,
  PtzStarted,
  PtzStopped,
  TargetAcquired,
  TargetLost,
  InferenceOverload,
  ModelError,
  RecordingStarted,
  RecordingStopped
};

struct Event {
  EventType type{EventType::ObjectDetected};
  std::chrono::system_clock::time_point occurredAt{std::chrono::system_clock::now()};
  std::uint64_t frameId{0};
  std::string sourceId;
  std::optional<Detection> detection;
  std::string message;
  std::unordered_map<std::string, std::string> metadata;
};

}  // namespace omnidetect
