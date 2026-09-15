#include "input/LibcameraSource.h"

#include "input/detail/OpenCvHelpers.h"

#include <algorithm>
#include <chrono>
#include <utility>

#ifdef OMNIDETECT_HAS_LIBCAMERA
#include <opencv2/videoio.hpp>
#endif

namespace omnidetect {

struct LibcameraSource::Impl {
  SourceConfig config;
  std::uint64_t nextFrame{1};
#ifdef OMNIDETECT_HAS_LIBCAMERA
  cv::VideoCapture capture;
#else
  bool opened{false};
#endif
};

LibcameraSource::LibcameraSource(SourceConfig config) : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
}

LibcameraSource::~LibcameraSource() { close(); }

bool LibcameraSource::open(std::string& error) noexcept {
  close();
#ifndef OMNIDETECT_HAS_LIBCAMERA
  error = "libcamera source was not enabled at build time";
  return false;
#else
  try {
    if (impl_->config.devicePath.find_first_of("\"\\\r\n") != std::string::npos) {
      error = "libcamera camera name contains unsupported characters";
      return false;
    }
    std::string pipeline = "libcamerasrc";
    if (!impl_->config.devicePath.empty()) {
      pipeline += " camera-name=\"" + impl_->config.devicePath + "\"";
    }
    pipeline += " ! video/x-raw,width=" + std::to_string(impl_->config.width) +
                ",height=" + std::to_string(impl_->config.height);
    if (impl_->config.fps > 0.0) {
      pipeline += ",framerate=" + std::to_string(static_cast<int>(impl_->config.fps)) + "/1";
    }
    pipeline += " ! videoconvert ! video/x-raw,format=BGR ! appsink drop=true max-buffers=1 sync=false";
    if (!impl_->capture.open(pipeline, cv::CAP_GSTREAMER)) {
      error = "Cannot open libcamera/GStreamer source for camera " + impl_->config.sourceId;
      return false;
    }
    impl_->nextFrame = 1;
    error.clear();
    return true;
  } catch (...) {
    close();
    error = "libcamera adapter failed while opening camera " + impl_->config.sourceId;
    return false;
  }
#endif
}

FrameReadResult LibcameraSource::read() noexcept {
#ifndef OMNIDETECT_HAS_LIBCAMERA
  return {FrameReadStatus::FatalError, {}, "libcamera source is unavailable in this build"};
#else
  if (!impl_->capture.isOpened()) return {FrameReadStatus::FatalError, {}, "libcamera source is not open"};
  try {
    cv::Mat matrix;
    if (!impl_->capture.read(matrix) || matrix.empty()) {
      return {FrameReadStatus::TemporaryError, {}, "libcamera frame read failed"};
    }
    auto image = detail::copyFromCvMat(matrix);
    if (!image) return {FrameReadStatus::TemporaryError, {}, "Unsupported libcamera pixel format"};
    Frame frame;
    frame.image = std::move(image);
    frame.frameId = impl_->nextFrame++;
    frame.sourceId = impl_->config.sourceId;
    frame.capturedAt = std::chrono::steady_clock::now();
    frame.wallClock = std::chrono::system_clock::now();
    return {FrameReadStatus::Ready, std::move(frame), {}};
  } catch (...) {
    return {FrameReadStatus::TemporaryError, {}, "libcamera adapter failed while reading a frame"};
  }
#endif
}

void LibcameraSource::close() noexcept {
#ifdef OMNIDETECT_HAS_LIBCAMERA
  if (impl_->capture.isOpened()) impl_->capture.release();
#else
  impl_->opened = false;
#endif
}

bool LibcameraSource::isOpened() const noexcept {
#ifdef OMNIDETECT_HAS_LIBCAMERA
  return impl_->capture.isOpened();
#else
  return impl_->opened;
#endif
}

SourceMetadata LibcameraSource::metadata() const {
  return {impl_->config.sourceId, "libcamera", impl_->config.width, impl_->config.height,
          impl_->config.fps};
}

}  // namespace omnidetect
