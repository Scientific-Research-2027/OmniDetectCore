#pragma once

#include "core/detection/DetectionResult.h"

#include <cstdint>
#include <vector>

namespace omnidetect {

struct TrackerConfig {
  float minimumIou{0.30F};
  std::uint32_t maxLostFrames{30};
  std::uint32_t minimumHits{1};
};

class ObjectTracker {
 public:
  explicit ObjectTracker(TrackerConfig config = {});
  void update(DetectionResult& result);
  void reset() noexcept;
  [[nodiscard]] std::size_t activeTrackCount() const noexcept { return tracks_.size(); }

 private:
  struct Track {
    std::uint64_t id{0};
    int classId{-1};
    BoundingBox bbox;
    std::uint32_t age{0};
    std::uint32_t hits{0};
    std::uint32_t lostFrames{0};
  };

  TrackerConfig config_;
  std::uint64_t nextId_{1};
  std::vector<Track> tracks_;
};

}  // namespace omnidetect

