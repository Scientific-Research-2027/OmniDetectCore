#include "control/AutoPtzController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace omnidetect {

AutoPtzController::AutoPtzController(std::string cameraId, AutoPtzConfig config,
                                     CameraControlWorker& worker)
    : cameraId_(std::move(cameraId)), config_(std::move(config)), worker_(worker) {}

int AutoPtzController::priorityOf(const Detection& detection) const noexcept {
  const auto found = std::find(config_.priorityClasses.begin(), config_.priorityClasses.end(), detection.classId);
  return found == config_.priorityClasses.end()
             ? static_cast<int>(config_.priorityClasses.size())
             : static_cast<int>(std::distance(config_.priorityClasses.begin(), found));
}

float AutoPtzController::score(const Detection& detection, const int width, const int height) const noexcept {
  const auto area = std::max(0.0F, detection.bbox.width) * std::max(0.0F, detection.bbox.height);
  const auto frameArea = std::max(1.0F, static_cast<float>(width) * static_cast<float>(height));
  const auto priorityBonus = static_cast<float>(config_.priorityClasses.size() -
                                                 std::min<std::size_t>(priorityOf(detection),
                                                                       config_.priorityClasses.size()));
  return priorityBonus * 10.0F + detection.confidence + std::sqrt(area / frameArea);
}

std::vector<Event> AutoPtzController::update(const Frame& frame, const DetectionResult& result) {
  std::vector<Event> events;
  if (!config_.enabled || !frame.valid() || !result.ok() || worker_.manualOverrideActive()) return events;
  const auto now = std::chrono::steady_clock::now();
  const Detection* current = nullptr;
  const Detection* best = nullptr;
  float currentScore = -std::numeric_limits<float>::infinity();
  float bestScore = -std::numeric_limits<float>::infinity();
  for (const auto& detection : result.detections) {
    if (!detection.trackId) continue;
    const auto candidateScore = score(detection, frame.width(), frame.height());
    if (targetTrackId_ && *detection.trackId == *targetTrackId_) {
      current = &detection;
      currentScore = candidateScore;
    }
    if (candidateScore > bestScore) {
      best = &detection;
      bestScore = candidateScore;
    }
  }

  if (targetTrackId_ && !current) {
    if (now - lastSeenAt_ < config_.lostTimeout) return events;
    worker_.enqueueAutomatic({CameraCommandType::Stop});
    events.push_back({EventType::TargetLost, std::chrono::system_clock::now(), result.frameId,
                      cameraId_, std::nullopt, "Auto PTZ target lost", {}});
    reset();
  }

  const Detection* target = current;
  const bool holdExpired = !targetTrackId_ || now - targetLockedAt_ >= config_.targetHold;
  if (!targetTrackId_) target = best;
  else if (current && best && best != current && holdExpired &&
           bestScore > currentScore + config_.switchMargin) {
    events.push_back({EventType::TargetLost, std::chrono::system_clock::now(), result.frameId,
                      cameraId_, *current, "Auto PTZ switched target", {}});
    target = best;
  }
  if (!target) return events;

  if (!targetTrackId_ || *target->trackId != *targetTrackId_) {
    targetTrackId_ = target->trackId;
    targetLockedAt_ = now;
    events.push_back({EventType::TargetAcquired, std::chrono::system_clock::now(), result.frameId,
                      cameraId_, *target, "Auto PTZ target acquired", {}});
  }
  lastSeenAt_ = now;
  if (now < nextCommandAt_) return events;

  const auto centerX = target->bbox.x + target->bbox.width * 0.5F;
  const auto centerY = target->bbox.y + target->bbox.height * 0.5F;
  const auto errorX = 2.0F * centerX / static_cast<float>(frame.width()) - 1.0F;
  const auto errorY = 2.0F * centerY / static_cast<float>(frame.height()) - 1.0F;
  const auto stopThreshold = std::max(0.0F, config_.deadZone - config_.hysteresis);
  panActive_ = panActive_ ? std::abs(errorX) > stopThreshold : std::abs(errorX) > config_.deadZone;
  tiltActive_ = tiltActive_ ? std::abs(errorY) > stopThreshold : std::abs(errorY) > config_.deadZone;
  const auto areaRatio = std::sqrt(std::max(0.0F, target->bbox.width * target->bbox.height) /
                                   std::max(1.0F, static_cast<float>(frame.width() * frame.height())));
  const auto zoomError = config_.targetBoxRatio - areaRatio;
  const auto zoomActive = std::abs(zoomError) > config_.zoomDeadBand;

  CameraControlCommand command;
  command.type = CameraCommandType::ContinuousMove;
  command.pan = panActive_ ? std::clamp(errorX * config_.panTiltGain, -config_.maximumSpeed,
                                       config_.maximumSpeed) : 0.0F;
  command.tilt = tiltActive_ ? std::clamp(errorY * config_.panTiltGain, -config_.maximumSpeed,
                                         config_.maximumSpeed) : 0.0F;
  command.zoom = zoomActive ? std::clamp(zoomError * config_.panTiltGain, -config_.maximumSpeed,
                                        config_.maximumSpeed) : 0.0F;
  const bool shouldMove = command.pan != 0.0F || command.tilt != 0.0F || command.zoom != 0.0F;
  if (shouldMove) {
    moving_ = worker_.enqueueAutomatic(command);
  } else if (moving_) {
    worker_.enqueueAutomatic({CameraCommandType::Stop});
    moving_ = false;
  }
  nextCommandAt_ = now + config_.commandInterval;
  return events;
}

void AutoPtzController::reset() noexcept {
  targetTrackId_.reset();
  targetLockedAt_ = {};
  lastSeenAt_ = {};
  nextCommandAt_ = {};
  moving_ = false;
  panActive_ = false;
  tiltActive_ = false;
}

}  // namespace omnidetect
