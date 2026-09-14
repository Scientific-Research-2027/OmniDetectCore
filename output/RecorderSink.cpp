#include "output/RecorderSink.h"

#include "core/logging/Logger.h"

#include <utility>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace omnidetect {

struct RecorderSink::Impl {
  RecorderConfig config;
  bool started{false};
#ifdef OMNIDETECT_HAS_OPENCV
  cv::VideoWriter writer;
#endif
};

RecorderSink::RecorderSink(RecorderConfig config) : impl_(std::make_unique<Impl>()) { impl_->config = std::move(config); }
RecorderSink::~RecorderSink() { stop(); }

bool RecorderSink::start(std::string& error) noexcept {
#ifndef OMNIDETECT_HAS_OPENCV
  error = "RecorderSink requires OpenCV videoio; install OpenCV and reconfigure CMake";
  return false;
#else
  if (impl_->config.path.empty() || impl_->config.fps <= 0.0) {
    error = "Recorder path must be non-empty and FPS must be positive";
    return false;
  }
  try {
    if (impl_->config.path.has_parent_path()) std::filesystem::create_directories(impl_->config.path.parent_path());
    impl_->started = true;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
#endif
}

void RecorderSink::consume(const Frame& frame, const DetectionResult& result) noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  if (!impl_->started || !frame.valid()) return;
  try {
    if (impl_->config.maxBytes > 0 && std::filesystem::exists(impl_->config.path) &&
        std::filesystem::file_size(impl_->config.path) >= impl_->config.maxBytes) {
      log::warning("Recorder quota reached; recording stopped");
      stop();
      return;
    }
    const auto& image = *frame.image;
    const int type = image.channels == 1 ? CV_8UC1 : CV_8UC3;
    cv::Mat borrowed(image.height, image.width, type, const_cast<std::uint8_t*>(image.bytes.data()), image.stride);
    cv::Mat bgr;
    if (image.channels == 1) cv::cvtColor(borrowed, bgr, cv::COLOR_GRAY2BGR);
    else if (image.format == PixelFormat::Rgb8) cv::cvtColor(borrowed, bgr, cv::COLOR_RGB2BGR);
    else bgr = borrowed.clone();
    for (const auto& detection : result.detections) {
      cv::rectangle(bgr, cv::Rect(static_cast<int>(detection.bbox.x), static_cast<int>(detection.bbox.y),
                                  static_cast<int>(detection.bbox.width), static_cast<int>(detection.bbox.height)),
                    cv::Scalar(40, 80, 255), 2);
    }
    if (!impl_->writer.isOpened()) {
      impl_->writer.open(impl_->config.path.string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                         impl_->config.fps, bgr.size(), true);
      if (!impl_->writer.isOpened()) {
        log::error("Recorder failed to open output codec/path");
        impl_->started = false;
        return;
      }
    }
    impl_->writer.write(bgr);
  } catch (const std::exception& exception) {
    log::error(std::string("Recorder failed: ") + exception.what());
  }
#else
  static_cast<void>(frame);
  static_cast<void>(result);
#endif
}

void RecorderSink::stop() noexcept {
#ifdef OMNIDETECT_HAS_OPENCV
  if (impl_->writer.isOpened()) impl_->writer.release();
#endif
  impl_->started = false;
}

}  // namespace omnidetect

