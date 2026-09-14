#include "input/ImageSource.h"

#include "input/detail/OpenCvHelpers.h"

#include <filesystem>
#include <utility>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/imgcodecs.hpp>
#endif

namespace omnidetect {

struct ImageSource::Impl {
  std::filesystem::path path;
  std::string sourceId;
  std::shared_ptr<const ImageBuffer> image;
  bool delivered{false};
};

ImageSource::ImageSource(std::filesystem::path path, std::string sourceId)
    : impl_(std::make_unique<Impl>(Impl{std::move(path), std::move(sourceId), {}, false})) {}
ImageSource::~ImageSource() = default;

bool ImageSource::open(std::string& error) noexcept {
  close();
#ifndef OMNIDETECT_HAS_OPENCV
  error = "ImageSource requires OpenCV imgcodecs; install OpenCV and reconfigure CMake";
  return false;
#else
  try {
    if (!std::filesystem::exists(impl_->path)) {
      error = "Image file does not exist: " + impl_->path.string();
      return false;
    }
    const auto matrix = cv::imread(impl_->path.string(), cv::IMREAD_UNCHANGED);
    impl_->image = detail::copyFromCvMat(matrix);
    if (!impl_->image) {
      error = "OpenCV could not decode a supported image: " + impl_->path.string();
      return false;
    }
    impl_->delivered = false;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    close();
    return false;
  }
#endif
}

FrameReadResult ImageSource::read() noexcept {
  if (!impl_->image) return {FrameReadStatus::FatalError, {}, "Image source is not open"};
  if (impl_->delivered) return {FrameReadStatus::EndOfStream, {}, {}};
  impl_->delivered = true;
  Frame frame;
  frame.image = impl_->image;
  frame.frameId = 1;
  frame.sourceId = impl_->sourceId;
  frame.capturedAt = std::chrono::steady_clock::now();
  frame.wallClock = std::chrono::system_clock::now();
  return {FrameReadStatus::Ready, std::move(frame), {}};
}

void ImageSource::close() noexcept {
  impl_->image.reset();
  impl_->delivered = false;
}

bool ImageSource::isOpened() const noexcept { return static_cast<bool>(impl_->image); }

SourceMetadata ImageSource::metadata() const {
  return {impl_->sourceId, "image", impl_->image ? impl_->image->width : 0,
          impl_->image ? impl_->image->height : 0, 0.0};
}

}  // namespace omnidetect

