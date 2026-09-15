#include "input/RtspSource.h"

#include "input/detail/OpenCvHelpers.h"

#include <chrono>
#include <utility>
#include <vector>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace omnidetect {

struct RtspSource::Impl {
  SourceConfig config;
  std::uint64_t nextFrame{1};
  unsigned consecutiveFailures{0};
#ifdef OMNIDETECT_HAS_OPENCV
  cv::VideoCapture capture;
#else
  bool opened{false};
#endif
};

RtspSource::RtspSource(SourceConfig config) : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
}

RtspSource::~RtspSource() { close(); }

bool RtspSource::open(std::string& error) noexcept {
  close();
#ifndef OMNIDETECT_HAS_OPENCV
  error = "RTSP source requires OpenCV videoio with FFmpeg support";
  return false;
#else
  try {
    const std::vector<int> parameters{cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 3000,
                                      cv::CAP_PROP_READ_TIMEOUT_MSEC, 1500};
    if (!impl_->capture.open(impl_->config.uri, cv::CAP_FFMPEG, parameters)) {
      error = "Cannot open RTSP source for camera " + impl_->config.sourceId;
      return false;
    }
    impl_->nextFrame = 1;
    impl_->consecutiveFailures = 0;
    error.clear();
    return true;
  } catch (...) {
    close();
    error = "RTSP adapter failed while opening camera " + impl_->config.sourceId;
    return false;
  }
#endif
}

FrameReadResult RtspSource::read() noexcept {
#ifndef OMNIDETECT_HAS_OPENCV
  return {FrameReadStatus::FatalError, {}, "RTSP source is unavailable without OpenCV/FFmpeg"};
#else
  if (!impl_->capture.isOpened()) {
    return {FrameReadStatus::FatalError, {}, "RTSP source is not open for camera " + impl_->config.sourceId};
  }
  try {
    cv::Mat matrix;
    if (!impl_->capture.read(matrix) || matrix.empty()) {
      ++impl_->consecutiveFailures;
      const auto status = impl_->consecutiveFailures >= 5U ? FrameReadStatus::FatalError
                                                            : FrameReadStatus::TemporaryError;
      return {status, {}, "RTSP frame read failed for camera " + impl_->config.sourceId};
    }
    impl_->consecutiveFailures = 0;
    auto image = detail::copyFromCvMat(matrix);
    if (!image) return {FrameReadStatus::TemporaryError, {}, "Unsupported RTSP pixel format"};
    Frame frame;
    frame.image = std::move(image);
    frame.frameId = impl_->nextFrame++;
    frame.sourceId = impl_->config.sourceId;
    frame.capturedAt = std::chrono::steady_clock::now();
    frame.wallClock = std::chrono::system_clock::now();
    return {FrameReadStatus::Ready, std::move(frame), {}};
  } catch (...) {
    return {FrameReadStatus::TemporaryError, {},
            "RTSP adapter failed while reading camera " + impl_->config.sourceId};
  }
#endif
}

void RtspSource::close() noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->capture.isOpened()) impl_->capture.release();
#else
  impl_->opened = false;
#endif
  impl_->consecutiveFailures = 0;
}

bool RtspSource::isOpened() const noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  return impl_->capture.isOpened();
#else
  return impl_->opened;
#endif
}

SourceMetadata RtspSource::metadata() const {
  SourceMetadata result{impl_->config.sourceId, "rtsp", impl_->config.width, impl_->config.height,
                        impl_->config.fps};
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->capture.isOpened()) {
    result.width = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_WIDTH));
    result.height = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    result.fps = impl_->capture.get(cv::CAP_PROP_FPS);
  }
#endif
  return result;
}

}  // namespace omnidetect
