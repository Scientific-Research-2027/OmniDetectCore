#pragma once

#include <string>

namespace omnidetect {

struct CameraControlCapabilities {
  bool continuousPanTilt{false};
  bool continuousZoom{false};
  bool absolutePosition{false};
  bool homePosition{false};
};

enum class CameraCommandType { ContinuousMove, AbsoluteMove, Stop, Home };
enum class CameraCommandOrigin { Automatic, Manual };

struct CameraControlCommand {
  CameraCommandType type{CameraCommandType::Stop};
  float pan{0.0F};
  float tilt{0.0F};
  float zoom{0.0F};
};

class ICameraControl {
 public:
  virtual ~ICameraControl() = default;
  virtual bool open(std::string& error) noexcept = 0;
  virtual bool execute(const CameraControlCommand& command, std::string& error) noexcept = 0;
  virtual void close() noexcept = 0;
  [[nodiscard]] virtual bool isReady() const noexcept = 0;
  [[nodiscard]] virtual CameraControlCapabilities capabilities() const noexcept = 0;
  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace omnidetect
