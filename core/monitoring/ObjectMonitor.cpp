#include "core/monitoring/ObjectMonitor.h"

#include <algorithm>
#include <utility>

namespace omnidetect {

ObjectMonitor::ObjectMonitor(MonitorConfig config) : config_(std::move(config)) {
  if (config_.confirmationFrames == 0) config_.confirmationFrames = 1;
  if (config_.lostFrames == 0) config_.lostFrames = 1;
}

bool ObjectMonitor::accepts(const Detection& detection) const {
  if (detection.confidence < config_.minimumConfidence) return false;
  if (!config_.targetClassIds.empty() && !config_.targetClassIds.contains(detection.classId)) return false;
  if (!config_.targetClassNames.empty() && !config_.targetClassNames.contains(detection.className)) return false;
  return true;
}

std::vector<Event> ObjectMonitor::update(const DetectionResult& result) {
  std::vector<Event> events;
  const auto best = std::max_element(result.detections.begin(), result.detections.end(),
                                     [this](const Detection& left, const Detection& right) {
                                       const auto leftScore = accepts(left) ? left.confidence : -1.0F;
                                       const auto rightScore = accepts(right) ? right.confidence : -1.0F;
                                       return leftScore < rightScore;
                                     });
  const bool observed = best != result.detections.end() && accepts(*best);
  if (observed) {
    lastDetection_ = *best;
    missedFrames_ = 0;
    ++observedFrames_;
    if (state_ == MonitorState::None || state_ == MonitorState::Lost) {
      state_ = MonitorState::Candidate;
      observedFrames_ = 1;
      events.push_back({EventType::ObjectDetected, std::chrono::system_clock::now(), result.frameId,
                        result.sourceId, lastDetection_, "Object candidate detected", {}});
    }
    if (state_ == MonitorState::Candidate && observedFrames_ >= config_.confirmationFrames) {
      state_ = MonitorState::Confirmed;
      events.push_back({EventType::ObjectConfirmed, std::chrono::system_clock::now(), result.frameId,
                        result.sourceId, lastDetection_, "Object presence confirmed", {}});
    }
  } else {
    observedFrames_ = 0;
    if (state_ == MonitorState::Candidate) {
      state_ = MonitorState::None;
      lastDetection_.reset();
    } else if (state_ == MonitorState::Confirmed) {
      ++missedFrames_;
      if (missedFrames_ >= config_.lostFrames) {
        state_ = MonitorState::Lost;
        events.push_back({EventType::ObjectLost, std::chrono::system_clock::now(), result.frameId,
                          result.sourceId, lastDetection_, "Confirmed object was lost", {}});
      }
    } else if (state_ == MonitorState::Lost) {
      state_ = MonitorState::None;
      missedFrames_ = 0;
      lastDetection_.reset();
    }
  }
  return events;
}

void ObjectMonitor::reset() noexcept {
  state_ = MonitorState::None;
  observedFrames_ = 0;
  missedFrames_ = 0;
  lastDetection_.reset();
}

}  // namespace omnidetect
