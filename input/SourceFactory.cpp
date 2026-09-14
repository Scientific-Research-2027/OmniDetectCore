#include "input/SourceFactory.h"

#include "input/CameraIdSource.h"
#include "input/ImageSource.h"
#include "input/VideoSource.h"
#include "input/WebcamSource.h"

namespace omnidetect {

std::unique_ptr<IFrameSource> createFrameSource(const SourceConfig& config, std::string& error) {
  switch (config.type) {
    case SourceType::Image:
      return std::make_unique<ImageSource>(config.path, config.sourceId);
    case SourceType::Webcam:
      return std::make_unique<WebcamSource>(WebcamConfig{config.device, config.width, config.height,
                                                         config.fps, config.sourceId});
    case SourceType::Video:
      return std::make_unique<VideoSource>(VideoSourceConfig{config.path, config.loop, config.sourceId});
    case SourceType::CameraId:
      return std::make_unique<CameraIdSource>(config.sourceId);
  }
  error = "Unknown frame source type";
  return nullptr;
}

}  // namespace omnidetect
