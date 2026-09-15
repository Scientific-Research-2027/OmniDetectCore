#include "core/tracking/ObjectTracker.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace omnidetect {
namespace {
float iou(const BoundingBox& left, const BoundingBox& right) noexcept {
  const auto x1 = std::max(left.x, right.x);
  const auto y1 = std::max(left.y, right.y);
  const auto x2 = std::min(left.x + left.width, right.x + right.width);
  const auto y2 = std::min(left.y + left.height, right.y + right.height);
  const auto overlap = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
  const auto unionArea = left.width * left.height + right.width * right.height - overlap;
  return unionArea > 0.0F ? overlap / unionArea : 0.0F;
}
}  // namespace

IouObjectTracker::IouObjectTracker(TrackerConfig config) : config_(config) {}

void IouObjectTracker::update(DetectionResult& result) {
  for (auto& track : tracks_) {
    ++track.age;
    ++track.lostFrames;
  }

  std::unordered_set<std::size_t> matchedTracks;
  for (auto& detection : result.detections) {
    float bestIou = config_.minimumIou;
    std::size_t bestIndex = tracks_.size();
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
      if (matchedTracks.contains(index) || tracks_[index].classId != detection.classId) continue;
      const auto overlap = iou(tracks_[index].bbox, detection.bbox);
      if (overlap >= bestIou) {
        bestIou = overlap;
        bestIndex = index;
      }
    }

    if (bestIndex == tracks_.size()) {
      tracks_.push_back({nextId_++, detection.classId, detection.bbox, 1, 1, 0});
      bestIndex = tracks_.size() - 1;
    } else {
      auto& track = tracks_[bestIndex];
      track.bbox = detection.bbox;
      track.lostFrames = 0;
      ++track.hits;
    }
    matchedTracks.insert(bestIndex);
    const auto& track = tracks_[bestIndex];
    if (track.hits >= config_.minimumHits) detection.trackId = track.id;
    detection.trackAge = track.age;
    detection.trackLost = false;
  }

  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                               [this](const Track& track) { return track.lostFrames > config_.maxLostFrames; }),
                tracks_.end());
}

void IouObjectTracker::reset() noexcept {
  tracks_.clear();
  nextId_ = 1;
}

}  // namespace omnidetect
