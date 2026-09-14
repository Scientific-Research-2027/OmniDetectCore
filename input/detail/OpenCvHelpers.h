#pragma once

#include "core/frame/Frame.h"

#include <cstring>
#include <memory>

#ifdef OMNIDETECT_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace omnidetect::detail {

inline std::shared_ptr<const ImageBuffer> copyFromCvMat(const cv::Mat& input) {
  if (input.empty()) return {};
  cv::Mat converted;
  if (input.depth() != CV_8U) {
    input.convertTo(converted, CV_8U);
  } else if (input.channels() == 4) {
    cv::cvtColor(input, converted, cv::COLOR_BGRA2BGR);
  } else {
    converted = input;
  }
  if (converted.channels() != 1 && converted.channels() != 3) return {};

  auto image = std::make_shared<ImageBuffer>();
  image->width = converted.cols;
  image->height = converted.rows;
  image->channels = converted.channels();
  image->stride = static_cast<std::size_t>(converted.cols * converted.channels());
  image->format = converted.channels() == 1 ? PixelFormat::Gray8 : PixelFormat::Bgr8;
  image->bytes.resize(image->stride * static_cast<std::size_t>(image->height));
  for (int row = 0; row < converted.rows; ++row) {
    std::memcpy(image->bytes.data() + static_cast<std::size_t>(row) * image->stride,
                converted.ptr(row), image->stride);
  }
  return image;
}

}  // namespace omnidetect::detail
#endif

