#include "core/preprocessing/Preprocessor.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace omnidetect {

BoundingBox LetterboxTransform::toOriginal(const BoundingBox& modelBox) const noexcept {
  if (scale <= std::numeric_limits<float>::epsilon() || originalWidth <= 0 || originalHeight <= 0) return {};
  const auto left = std::clamp((modelBox.x - padX) / scale, 0.0F, static_cast<float>(originalWidth));
  const auto top = std::clamp((modelBox.y - padY) / scale, 0.0F, static_cast<float>(originalHeight));
  const auto right = std::clamp((modelBox.x + modelBox.width - padX) / scale, 0.0F, static_cast<float>(originalWidth));
  const auto bottom = std::clamp((modelBox.y + modelBox.height - padY) / scale, 0.0F, static_cast<float>(originalHeight));
  return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}

Preprocessor::Preprocessor(PreprocessConfig config) : config_(config) {}

PreprocessedFrame Preprocessor::process(const Frame& frame) const noexcept {
  PreprocessedFrame output;
  if (!frame.valid()) {
    output.error = "Frame has no valid image buffer";
    return output;
  }
  if (config_.targetWidth <= 0 || config_.targetHeight <= 0 || config_.normalizationScale <= 0.0F) {
    output.error = "Invalid preprocessing configuration";
    return output;
  }

  try {
    const auto& source = *frame.image;
    if (source.channels != 1 && source.channels != 3) {
      output.error = "Only Gray8 and three-channel RGB/BGR frames are supported";
      return output;
    }

    const auto scale = std::min(static_cast<float>(config_.targetWidth) / static_cast<float>(source.width),
                                static_cast<float>(config_.targetHeight) / static_cast<float>(source.height));
    const auto resizedWidth = std::max(1, static_cast<int>(std::round(static_cast<float>(source.width) * scale)));
    const auto resizedHeight = std::max(1, static_cast<int>(std::round(static_cast<float>(source.height) * scale)));
    const auto left = (config_.targetWidth - resizedWidth) / 2;
    const auto top = (config_.targetHeight - resizedHeight) / 2;

    output.transform = {source.width, source.height, config_.targetWidth, config_.targetHeight,
                        scale, static_cast<float>(left), static_cast<float>(top)};
    output.input.shape = {1, 3, config_.targetHeight, config_.targetWidth};
    const auto plane = static_cast<std::size_t>(config_.targetWidth) * static_cast<std::size_t>(config_.targetHeight);
    output.input.values.assign(plane * 3U, config_.padValue * config_.normalizationScale);

    for (int y = 0; y < resizedHeight; ++y) {
      const auto sourceY = std::clamp((static_cast<float>(y) + 0.5F) / scale - 0.5F, 0.0F,
                                      static_cast<float>(source.height - 1));
      const auto y0 = static_cast<int>(std::floor(sourceY));
      const auto y1 = std::min(y0 + 1, source.height - 1);
      const auto wy = sourceY - static_cast<float>(y0);
      for (int x = 0; x < resizedWidth; ++x) {
        const auto sourceX = std::clamp((static_cast<float>(x) + 0.5F) / scale - 0.5F, 0.0F,
                                        static_cast<float>(source.width - 1));
        const auto x0 = static_cast<int>(std::floor(sourceX));
        const auto x1 = std::min(x0 + 1, source.width - 1);
        const auto wx = sourceX - static_cast<float>(x0);
        const auto destination = static_cast<std::size_t>(top + y) * static_cast<std::size_t>(config_.targetWidth) +
                                 static_cast<std::size_t>(left + x);

        for (int channel = 0; channel < 3; ++channel) {
          const auto read = [&](const int sx, const int sy) {
            const auto* row = source.bytes.data() + static_cast<std::size_t>(sy) * source.stride;
            if (source.channels == 1) return static_cast<float>(row[sx]);
            int sourceChannel = channel;
            const bool needsSwap = config_.outputRgb && source.format == PixelFormat::Bgr8;
            const bool reverseSwap = !config_.outputRgb && source.format == PixelFormat::Rgb8;
            if ((needsSwap || reverseSwap) && channel != 1) sourceChannel = 2 - channel;
            return static_cast<float>(row[static_cast<std::size_t>(sx * source.channels + sourceChannel)]);
          };
          const auto topValue = read(x0, y0) * (1.0F - wx) + read(x1, y0) * wx;
          const auto bottomValue = read(x0, y1) * (1.0F - wx) + read(x1, y1) * wx;
          output.input.values[static_cast<std::size_t>(channel) * plane + destination] =
              (topValue * (1.0F - wy) + bottomValue * wy) * config_.normalizationScale;
        }
      }
    }
  } catch (const std::exception& exception) {
    output.input = {};
    output.error = std::string("Preprocessing failed: ") + exception.what();
  } catch (...) {
    output.input = {};
    output.error = "Preprocessing failed with an unknown error";
  }
  return output;
}

}  // namespace omnidetect

