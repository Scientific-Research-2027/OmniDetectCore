#include "core/monitoring/MultiObjectMonitor.h"

#include <chrono>
#include <unordered_set>
#include <utility>

namespace omnidetect {

ClassFilterMonitorPolicy::ClassFilterMonitorPolicy(MonitorConfig config) : config_(std::move(config)) {}

bool ClassFilterMonitorPolicy::accepts(const Detection& detection) const noexcept {
  if (detection.confidence < config_.minimumConfidence) return false;
  if (!config_.targetClassIds.empty() && !config_.targetClassIds.contains(detection.classId)) return false;
  if (!config_.targetClassNames.empty() && !config_.targetClassNames.contains(detection.className)) return false;
  return true;
}

std::size_t MultiObjectMonitor::ObjectKeyHash::operator()(const ObjectKey& key) const noexcept {
  const auto left = std::hash<std::string>{}(key.cameraId);
  const auto right = std::hash<std::uint64_t>{}(key.trackId);
  return left ^ (right + 0x9e3779b9U + (left << 6U) + (left >> 2U));
}

MultiObjectMonitor::MultiObjectMonitor(MonitorConfig config, std::shared_ptr<IMonitorPolicy> policy)
    : config_(std::move(config)), policy_(std::move(policy)) {
  if (config_.confirmationFrames == 0) config_.confirmationFrames = 1;
  if (config_.lostFrames == 0) config_.lostFrames = 1;
  if (!policy_) policy_ = std::make_shared<ClassFilterMonitorPolicy>(config_);
}

std::vector<Event> MultiObjectMonitor::update(const DetectionResult& result) {
  std::vector<Event> events;
  std::vector<const Detection*> acceptedDetections;
  acceptedDetections.reserve(result.detections.size());
  for (const auto& detection : result.detections) {
    if (detection.trackId && policy_->accepts(detection)) acceptedDetections.push_back(&detection);
  }
  std::unordered_set<ObjectKey, ObjectKeyHash> observed;
  const auto now = std::chrono::system_clock::now();
  const std::scoped_lock lock(mutex_);

  for (const auto* detectionPointer : acceptedDetections) {
    const auto& detection = *detectionPointer;
    ObjectKey key{result.sourceId, *detection.trackId};
    observed.insert(key);
    const auto [iterator, inserted] = states_.try_emplace(
        key, MonitoredObjectState{result.sourceId, *detection.trackId, detection.classId, detection.className,
                                  MonitorState::Candidate, 0, 0, result.frameId, detection});
    auto& state = iterator->second;
    state.classId = detection.classId;
    state.className = detection.className;
    state.lastDetection = detection;
    state.lastFrameId = result.frameId;
    state.missedFrames = 0;
    ++state.observedFrames;
    if (inserted) {
      events.push_back({EventType::ObjectDetected, now, result.frameId, result.sourceId, detection,
                        "Tracked object candidate detected", {}});
    }
    if (state.state == MonitorState::Candidate && state.observedFrames >= config_.confirmationFrames) {
      state.state = MonitorState::Confirmed;
      events.push_back({EventType::ObjectConfirmed, now, result.frameId, result.sourceId, detection,
                        "Tracked object presence confirmed", {}});
    }
  }

  for (auto iterator = states_.begin(); iterator != states_.end();) {
    auto& state = iterator->second;
    if (state.cameraId != result.sourceId || observed.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    ++state.missedFrames;
    if (state.state == MonitorState::Candidate) {
      iterator = states_.erase(iterator);
    } else if (state.missedFrames >= config_.lostFrames) {
      state.state = MonitorState::Lost;
      events.push_back({EventType::ObjectLost, now, result.frameId, result.sourceId, state.lastDetection,
                        "Confirmed tracked object was lost", {}});
      iterator = states_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  return events;
}

void MultiObjectMonitor::reset() noexcept {
  const std::scoped_lock lock(mutex_);
  states_.clear();
}

std::vector<MonitoredObjectState> MultiObjectMonitor::snapshot() const {
  const std::scoped_lock lock(mutex_);
  std::vector<MonitoredObjectState> result;
  result.reserve(states_.size());
  for (const auto& [unused, state] : states_) {
    static_cast<void>(unused);
    result.push_back(state);
  }
  return result;
}

}  // namespace omnidetect
