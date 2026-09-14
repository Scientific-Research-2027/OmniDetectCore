#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace omnidetect {

// Bounding boxes always use original-image pixel coordinates.
struct BoundingBox {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
};

struct Detection {
  int classId{-1};
  std::string className;
  float confidence{0.0F};
  BoundingBox bbox;
  std::optional<std::uint64_t> trackId;
  std::uint32_t trackAge{0};
  bool trackLost{false};
};

}  // namespace omnidetect

