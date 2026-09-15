#include "control/V4l2CameraControl.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace omnidetect {

struct V4l2CameraControl::Impl {
  std::string devicePath;
  std::atomic<bool> ready{false};
  CameraControlCapabilities capabilities;
#ifdef __linux__
  int descriptor{-1};
#endif
};

#ifdef __linux__
namespace {
bool queryControl(const int descriptor, const std::uint32_t id, v4l2_queryctrl& query) {
  query = {};
  query.id = id;
  return ioctl(descriptor, VIDIOC_QUERYCTRL, &query) == 0 && (query.flags & V4L2_CTRL_FLAG_DISABLED) == 0;
}

bool setRaw(const int descriptor, const std::uint32_t id, const int value) {
  v4l2_control control{};
  control.id = id;
  control.value = value;
  return ioctl(descriptor, VIDIOC_S_CTRL, &control) == 0;
}

bool setNormalized(const int descriptor, const std::uint32_t id, const float value) {
  v4l2_queryctrl query{};
  if (!queryControl(descriptor, id, query)) return false;
  const auto normalized = std::clamp(value, -1.0F, 1.0F);
  const auto position = static_cast<float>(query.minimum) + (normalized + 1.0F) * 0.5F *
                        static_cast<float>(query.maximum - query.minimum);
  return setRaw(descriptor, id, static_cast<int>(std::lround(position)));
}
}  // namespace
#endif

V4l2CameraControl::V4l2CameraControl(std::string devicePath) : impl_(std::make_unique<Impl>()) {
  impl_->devicePath = std::move(devicePath);
}

V4l2CameraControl::~V4l2CameraControl() { close(); }

bool V4l2CameraControl::open(std::string& error) noexcept {
  close();
#ifndef __linux__
  error = "V4L2 camera control is available only on Linux";
  return false;
#else
  if (impl_->devicePath.empty() || !impl_->devicePath.starts_with("/dev/video")) {
    error = "V4L2 control requires a /dev/video* device";
    return false;
  }
  impl_->descriptor = ::open(impl_->devicePath.c_str(), O_RDWR | O_NONBLOCK);
  if (impl_->descriptor < 0) {
    error = "Cannot open V4L2 control device";
    return false;
  }
  v4l2_queryctrl query{};
#ifdef V4L2_CID_PAN_SPEED
  impl_->capabilities.continuousPanTilt = queryControl(impl_->descriptor, V4L2_CID_PAN_SPEED, query);
#endif
#ifdef V4L2_CID_TILT_SPEED
  impl_->capabilities.continuousPanTilt = impl_->capabilities.continuousPanTilt ||
                                          queryControl(impl_->descriptor, V4L2_CID_TILT_SPEED, query);
#endif
#ifdef V4L2_CID_ZOOM_CONTINUOUS
  impl_->capabilities.continuousZoom = queryControl(impl_->descriptor, V4L2_CID_ZOOM_CONTINUOUS, query);
#endif
  impl_->capabilities.absolutePosition = queryControl(impl_->descriptor, V4L2_CID_PAN_ABSOLUTE, query) ||
                                          queryControl(impl_->descriptor, V4L2_CID_TILT_ABSOLUTE, query) ||
                                          queryControl(impl_->descriptor, V4L2_CID_ZOOM_ABSOLUTE, query);
  impl_->capabilities.homePosition = impl_->capabilities.absolutePosition;
  impl_->ready.store(true, std::memory_order_release);
  error.clear();
  return true;
#endif
}

bool V4l2CameraControl::execute(const CameraControlCommand& command, std::string& error) noexcept {
#ifndef __linux__
  static_cast<void>(command);
  error = "V4L2 camera control is unavailable on this platform";
  return false;
#else
  if (!isReady()) {
    error = "V4L2 camera control is not ready";
    return false;
  }
  bool applied = false;
  if (command.type == CameraCommandType::ContinuousMove || command.type == CameraCommandType::Stop) {
    const auto pan = command.type == CameraCommandType::Stop ? 0.0F : command.pan;
    const auto tilt = command.type == CameraCommandType::Stop ? 0.0F : command.tilt;
    const auto zoom = command.type == CameraCommandType::Stop ? 0.0F : command.zoom;
#ifdef V4L2_CID_PAN_SPEED
    applied = setNormalized(impl_->descriptor, V4L2_CID_PAN_SPEED, pan) || applied;
#endif
#ifdef V4L2_CID_TILT_SPEED
    applied = setNormalized(impl_->descriptor, V4L2_CID_TILT_SPEED, tilt) || applied;
#endif
#ifdef V4L2_CID_ZOOM_CONTINUOUS
    applied = setNormalized(impl_->descriptor, V4L2_CID_ZOOM_CONTINUOUS, zoom) || applied;
#endif
  } else {
    const auto pan = command.type == CameraCommandType::Home ? 0.0F : command.pan;
    const auto tilt = command.type == CameraCommandType::Home ? 0.0F : command.tilt;
    const auto zoom = command.type == CameraCommandType::Home ? -1.0F : command.zoom;
    applied = setNormalized(impl_->descriptor, V4L2_CID_PAN_ABSOLUTE, pan) || applied;
    applied = setNormalized(impl_->descriptor, V4L2_CID_TILT_ABSOLUTE, tilt) || applied;
    applied = setNormalized(impl_->descriptor, V4L2_CID_ZOOM_ABSOLUTE, zoom) || applied;
  }
  if (!applied) {
    error = "V4L2 device does not expose a compatible PTZ control";
    return false;
  }
  error.clear();
  return true;
#endif
}

void V4l2CameraControl::close() noexcept {
#ifdef __linux__
  if (impl_->descriptor >= 0) ::close(impl_->descriptor);
  impl_->descriptor = -1;
#endif
  impl_->ready.store(false, std::memory_order_release);
  impl_->capabilities = {};
}

bool V4l2CameraControl::isReady() const noexcept { return impl_->ready.load(std::memory_order_acquire); }

CameraControlCapabilities V4l2CameraControl::capabilities() const noexcept { return impl_->capabilities; }

}  // namespace omnidetect
