#include "input/CameraIdSource.h"

#include <utility>

namespace omnidetect {

CameraIdSource::CameraIdSource(std::string cameraId, std::unique_ptr<ICameraIdAdapter> adapter)
    : cameraId_(std::move(cameraId)), adapter_(std::move(adapter)) {}
CameraIdSource::~CameraIdSource() { close(); }

bool CameraIdSource::open(std::string& error) noexcept {
  if (!adapter_) {
    error = "No Camera ID protocol adapter is configured; implement ICameraIdAdapter for the real SDK/transport";
    return false;
  }
  return adapter_->connect(cameraId_, error);
}

FrameReadResult CameraIdSource::read() noexcept {
  if (!adapter_ || !adapter_->connected()) return {FrameReadStatus::FatalError, {}, "Camera ID source is not connected"};
  return adapter_->receive();
}

void CameraIdSource::close() noexcept {
  if (adapter_) adapter_->disconnect();
}

bool CameraIdSource::isOpened() const noexcept { return adapter_ && adapter_->connected(); }
SourceMetadata CameraIdSource::metadata() const { return {cameraId_, "camera_id", 0, 0, 0.0}; }

}  // namespace omnidetect

