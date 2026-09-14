#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnidetect {

enum class PixelFormat { Bgr8, Rgb8, Gray8 };

struct ImageBuffer {
  int width{0};
  int height{0};
  int channels{0};
  std::size_t stride{0};
  PixelFormat format{PixelFormat::Bgr8};
  std::vector<std::uint8_t> bytes;

  [[nodiscard]] bool valid() const noexcept {
    return width > 0 && height > 0 && channels > 0 &&
           stride >= static_cast<std::size_t>(width * channels) &&
           bytes.size() >= stride * static_cast<std::size_t>(height);
  }
};

struct Frame {
  using MonotonicTime = std::chrono::steady_clock::time_point;
  using WallTime = std::chrono::system_clock::time_point;

  std::shared_ptr<const ImageBuffer> image;
  std::uint64_t frameId{0};
  MonotonicTime capturedAt{std::chrono::steady_clock::now()};
  WallTime wallClock{std::chrono::system_clock::now()};
  std::string sourceId;
  std::unordered_map<std::string, std::string> metadata;

  [[nodiscard]] int width() const noexcept { return image ? image->width : 0; }
  [[nodiscard]] int height() const noexcept { return image ? image->height : 0; }
  [[nodiscard]] bool valid() const noexcept { return image && image->valid(); }
};

}  // namespace omnidetect

