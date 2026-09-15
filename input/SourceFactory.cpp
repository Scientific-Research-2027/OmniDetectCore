#include "input/SourceFactory.h"

#include "input/CameraIdSource.h"
#include "input/ImageSource.h"
#include "input/LibcameraSource.h"
#include "input/RtspSource.h"
#include "input/VideoSource.h"
#include "input/WebcamSource.h"

namespace omnidetect {

std::unique_ptr<IFrameSource> createFrameSource(const SourceConfig& config, std::string& error) {
  switch (config.type) {
    case SourceType::Image:
      return std::make_unique<ImageSource>(config.path, config.sourceId);
    case SourceType::Webcam:
      return std::make_unique<WebcamSource>(WebcamConfig{config.device, config.devicePath, config.width,
                                                         config.height, config.fps, config.sourceId});
    case SourceType::Video:
      return std::make_unique<VideoSource>(VideoSourceConfig{config.path, config.loop, config.sourceId});
    case SourceType::CameraId:
      return std::make_unique<CameraIdSource>(config.sourceId);
    case SourceType::Rtsp:
      return std::make_unique<RtspSource>(config);
    case SourceType::Libcamera:
      return std::make_unique<LibcameraSource>(config);
  }
  error = "Unknown frame source type";
  return nullptr;
}

}  // namespace omnidetect
