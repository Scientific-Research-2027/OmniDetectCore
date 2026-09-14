#include "output/ImageSink.h"

#include "core/logging/Logger.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace omnidetect {
namespace {

void setPixel(ImageBuffer& image, const int x, const int y, const std::uint8_t red,
              const std::uint8_t green, const std::uint8_t blue) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
  auto* pixel = image.bytes.data() + static_cast<std::size_t>(y) * image.stride +
                static_cast<std::size_t>(x * image.channels);
  if (image.channels == 1) {
    pixel[0] = static_cast<std::uint8_t>((static_cast<unsigned>(red) + green + blue) / 3U);
  } else if (image.format == PixelFormat::Rgb8) {
    pixel[0] = red; pixel[1] = green; pixel[2] = blue;
  } else {
    pixel[0] = blue; pixel[1] = green; pixel[2] = red;
  }
}

void drawBox(ImageBuffer& image, const BoundingBox& box) {
  const auto left = std::clamp(static_cast<int>(box.x), 0, image.width - 1);
  const auto top = std::clamp(static_cast<int>(box.y), 0, image.height - 1);
  const auto right = std::clamp(static_cast<int>(box.x + box.width), 0, image.width - 1);
  const auto bottom = std::clamp(static_cast<int>(box.y + box.height), 0, image.height - 1);
  for (int thickness = 0; thickness < 2; ++thickness) {
    for (int x = left; x <= right; ++x) {
      setPixel(image, x, top + thickness, 255, 80, 40);
      setPixel(image, x, bottom - thickness, 255, 80, 40);
    }
    for (int y = top; y <= bottom; ++y) {
      setPixel(image, left + thickness, y, 255, 80, 40);
      setPixel(image, right - thickness, y, 255, 80, 40);
    }
  }
}

std::string escaped(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    if (character == '"' || character == '\\') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

}  // namespace

ImageSink::ImageSink(std::filesystem::path outputPath, const bool appendFrameId)
    : outputPath_(std::move(outputPath)), appendFrameId_(appendFrameId) {}

bool ImageSink::start(std::string& error) noexcept {
  try {
    if (outputPath_.empty()) {
      error = "ImageSink output path is empty";
      return false;
    }
    if (outputPath_.has_parent_path()) std::filesystem::create_directories(outputPath_.parent_path());
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

void ImageSink::consume(const Frame& frame, const DetectionResult& result) noexcept {
  if (!frame.valid()) return;
  try {
    auto path = outputPath_;
    if (appendFrameId_) {
      path = outputPath_.parent_path() /
             (outputPath_.stem().string() + "_" + std::to_string(frame.frameId) + ".ppm");
    } else {
      path.replace_extension(".ppm");
    }
    ImageBuffer rendered = *frame.image;
    for (const auto& detection : result.detections) drawBox(rendered, detection.bbox);

    std::ofstream image(path, std::ios::binary);
    if (!image) throw std::runtime_error("Cannot write " + path.string());
    image << "P6\n" << rendered.width << ' ' << rendered.height << "\n255\n";
    for (int y = 0; y < rendered.height; ++y) {
      const auto* row = rendered.bytes.data() + static_cast<std::size_t>(y) * rendered.stride;
      for (int x = 0; x < rendered.width; ++x) {
        if (rendered.channels == 1) {
          image.put(static_cast<char>(row[x])); image.put(static_cast<char>(row[x])); image.put(static_cast<char>(row[x]));
        } else if (rendered.format == PixelFormat::Bgr8) {
          image.put(static_cast<char>(row[x * 3 + 2])); image.put(static_cast<char>(row[x * 3 + 1])); image.put(static_cast<char>(row[x * 3]));
        } else {
          image.write(reinterpret_cast<const char*>(row + x * 3), 3);
        }
      }
    }

    auto metadataPath = path;
    metadataPath.replace_extension(".json");
    std::ofstream metadata(metadataPath);
    metadata << "{\n  \"frame_id\": " << result.frameId << ",\n  \"source_id\": \"" << escaped(result.sourceId)
             << "\",\n  \"detections\": [";
    for (std::size_t index = 0; index < result.detections.size(); ++index) {
      const auto& detection = result.detections[index];
      metadata << (index == 0 ? "\n" : ",\n") << "    {\"class_id\": " << detection.classId
               << ", \"class_name\": \"" << escaped(detection.className) << "\", \"confidence\": "
               << std::fixed << std::setprecision(4) << detection.confidence << ", \"bbox\": ["
               << detection.bbox.x << ", " << detection.bbox.y << ", " << detection.bbox.width << ", "
               << detection.bbox.height << "]}";
    }
    metadata << (result.detections.empty() ? "" : "\n  ") << "]\n}\n";
    const std::scoped_lock lock(mutex_);
    lastError_.clear();
  } catch (const std::exception& exception) {
    const std::scoped_lock lock(mutex_);
    lastError_ = exception.what();
    log::error(std::string("ImageSink failed: ") + exception.what());
  }
}

std::string ImageSink::lastError() const {
  const std::scoped_lock lock(mutex_);
  return lastError_;
}

}  // namespace omnidetect

