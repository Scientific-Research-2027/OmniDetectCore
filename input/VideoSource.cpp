#include "input/VideoSource.h"

#include "input/detail/OpenCvHelpers.h"

#include <filesystem>
#include <utility>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace omnidetect {

struct VideoSource::Impl {
  VideoSourceConfig config;
  std::uint64_t nextFrame{1};
#ifdef OMNIDETECT_HAS_OPENCV
  cv::VideoCapture capture;
#else
  bool opened{false};
#endif
};

VideoSource::VideoSource(VideoSourceConfig config) : impl_(std::make_unique<Impl>()) { impl_->config = std::move(config); }
VideoSource::~VideoSource() { close(); }

bool VideoSource::open(std::string& error) noexcept {
  close();
#ifndef OMNIDETECT_HAS_OPENCV
  error = "VideoSource requires OpenCV videoio; install OpenCV and reconfigure CMake";
  return false;
#else
  try {
    if (!std::filesystem::exists(impl_->config.path)) {
      error = "Video file does not exist: " + impl_->config.path.string();
      return false;
    }
    if (!impl_->capture.open(impl_->config.path.string())) {
      error = "OpenCV cannot open video: " + impl_->config.path.string();
      return false;
    }
    impl_->nextFrame = 1;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    close();
    return false;
  }
#endif
}

FrameReadResult VideoSource::read() noexcept {
#ifndef OMNIDETECT_HAS_OPENCV
  return {FrameReadStatus::FatalError, {}, "VideoSource is unavailable without OpenCV"};
#else
  if (!impl_->capture.isOpened()) return {FrameReadStatus::FatalError, {}, "Video is not open"};
  try {
    cv::Mat matrix;
    if (!impl_->capture.read(matrix) || matrix.empty()) {
      if (!impl_->config.loop) return {FrameReadStatus::EndOfStream, {}, {}};
      impl_->capture.set(cv::CAP_PROP_POS_FRAMES, 0.0);
      if (!impl_->capture.read(matrix) || matrix.empty()) {
        return {FrameReadStatus::FatalError, {}, "Video loop seek/read failed"};
      }
    }
    auto image = detail::copyFromCvMat(matrix);
    if (!image) return {FrameReadStatus::TemporaryError, {}, "Unsupported video pixel format"};
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

void VideoSource::close() noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->capture.isOpened()) impl_->capture.release();
#else
  impl_->opened = false;
#endif
}

bool VideoSource::isOpened() const noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  return impl_->capture.isOpened();
#else
  return impl_->opened;
#endif
}

SourceMetadata VideoSource::metadata() const {
  SourceMetadata metadata{impl_->config.sourceId, "video", 0, 0, 0.0};
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

