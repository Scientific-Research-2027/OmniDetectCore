#include "input/WebcamSource.h"

#include "input/detail/OpenCvHelpers.h"

#include <utility>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace omnidetect {

struct WebcamSource::Impl {
  WebcamConfig config;
  std::uint64_t nextFrame{1};
  int consecutiveFailures{0};
#ifdef OMNIDETECT_HAS_OPENCV
  cv::VideoCapture capture;
#else
  bool opened{false};
#endif
};

WebcamSource::WebcamSource(WebcamConfig config) : impl_(std::make_unique<Impl>()) { impl_->config = std::move(config); }
WebcamSource::~WebcamSource() { close(); }

bool WebcamSource::open(std::string& error) noexcept {
  close();
#ifndef OMNIDETECT_HAS_OPENCV
  error = "WebcamSource requires OpenCV videoio; install OpenCV and reconfigure CMake";
  return false;
#else
  try {
    const bool opened = impl_->config.devicePath.empty()
                            ? impl_->capture.open(impl_->config.deviceIndex)
                            : impl_->capture.open(impl_->config.devicePath);
    if (!opened) {
      error = impl_->config.devicePath.empty()
                  ? "Cannot open camera device " + std::to_string(impl_->config.deviceIndex)
                  : "Cannot open camera device path " + impl_->config.devicePath;
      return false;
    }
    if (impl_->config.width > 0) impl_->capture.set(cv::CAP_PROP_FRAME_WIDTH, impl_->config.width);
    if (impl_->config.height > 0) impl_->capture.set(cv::CAP_PROP_FRAME_HEIGHT, impl_->config.height);
    if (impl_->config.fps > 0.0) impl_->capture.set(cv::CAP_PROP_FPS, impl_->config.fps);
    impl_->nextFrame = 1;
    impl_->consecutiveFailures = 0;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    close();
    return false;
  }
#endif
}

FrameReadResult WebcamSource::read() noexcept {
#ifndef OMNIDETECT_HAS_OPENCV
  return {FrameReadStatus::FatalError, {}, "WebcamSource is unavailable without OpenCV"};
#else
  if (!impl_->capture.isOpened()) return {FrameReadStatus::FatalError, {}, "Camera is not open"};
  try {
    cv::Mat matrix;
    if (!impl_->capture.read(matrix) || matrix.empty()) {
      ++impl_->consecutiveFailures;
      const auto status = impl_->consecutiveFailures >= 30 ? FrameReadStatus::FatalError : FrameReadStatus::TemporaryError;
      return {status, {}, "Camera frame read failed"};
    }
    impl_->consecutiveFailures = 0;
    auto image = detail::copyFromCvMat(matrix);
    if (!image) return {FrameReadStatus::TemporaryError, {}, "Unsupported camera pixel format"};
    Frame frame;
    frame.image = std::move(image);
    frame.frameId = impl_->nextFrame++;
    frame.sourceId = impl_->config.sourceId;
    frame.capturedAt = std::chrono::steady_clock::now();
    frame.wallClock = std::chrono::system_clock::now();
    return {FrameReadStatus::Ready, std::move(frame), {}};
  } catch (const std::exception& exception) {
    return {FrameReadStatus::TemporaryError, {}, exception.what()};
  }
#endif
}

void WebcamSource::close() noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->capture.isOpened()) impl_->capture.release();
#else
  impl_->opened = false;
#endif
  impl_->consecutiveFailures = 0;
}

bool WebcamSource::isOpened() const noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  return impl_->capture.isOpened();
#else
  return impl_->opened;
#endif
}

SourceMetadata WebcamSource::metadata() const {
  SourceMetadata metadata{impl_->config.sourceId, "webcam", impl_->config.width, impl_->config.height, impl_->config.fps};
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->capture.isOpened()) {
    metadata.width = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_WIDTH));
    metadata.height = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    metadata.fps = impl_->capture.get(cv::CAP_PROP_FPS);
  }
#endif
  return metadata;
}

}  // namespace omnidetect
