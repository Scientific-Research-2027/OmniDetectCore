#include "tests/TestHarness.h"

#include "control/AutoPtzController.h"
#include "control/CameraControlWorker.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace omnidetect;
using namespace std::chrono_literals;

struct ControlState {
  std::atomic<bool> ready{false};
  mutable std::mutex mutex;
  std::vector<CameraControlCommand> commands;
};

class MockCameraControl final : public ICameraControl {
 public:
  explicit MockCameraControl(std::shared_ptr<ControlState> state) : state_(std::move(state)) {}
  bool open(std::string& error) noexcept override {
    state_->ready.store(true);
    error.clear();
    return true;
  }
  bool execute(const CameraControlCommand& command, std::string& error) noexcept override {
    const std::scoped_lock lock(state_->mutex);
    state_->commands.push_back(command);
    error.clear();
    return true;
  }
  void close() noexcept override { state_->ready.store(false); }
  [[nodiscard]] bool isReady() const noexcept override { return state_->ready.load(); }
  [[nodiscard]] CameraControlCapabilities capabilities() const noexcept override {
    return {true, true, false, true};
  }
  [[nodiscard]] std::string name() const override { return "mock-control"; }

 private:
  std::shared_ptr<ControlState> state_;
};

Frame ptzFrame() {
  auto image = std::make_shared<ImageBuffer>();
  image->width = 100;
  image->height = 100;
  image->channels = 3;
  image->stride = 300;
  image->bytes.assign(30000, 0);
  return {std::move(image), 1, std::chrono::steady_clock::now(), std::chrono::system_clock::now(), "ptz", {}};
}

template <typename Predicate>
bool waitForControl(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(2ms);
  }
  return predicate();
}

OMNI_TEST(CameraControlWorkerBoundsQueueAndHonorsManualOverride) {
  EventEngine events;
  auto state = std::make_shared<ControlState>();
  CameraControlConfig config;
  config.queueSize = 2;
  config.maximumCommandsPerSecond = 1000.0;
  config.manualOverrideHold = 30ms;
  CameraControlWorker worker("ptz", config, std::make_unique<MockCameraControl>(state), events);
  std::string error;
  OMNI_REQUIRE(worker.start(error));
  OMNI_REQUIRE(worker.enqueueManual({CameraCommandType::ContinuousMove, 0.4F, 0.0F, 0.0F}));
  OMNI_REQUIRE(!worker.enqueueAutomatic({CameraCommandType::ContinuousMove, -0.4F, 0.0F, 0.0F}));
  OMNI_REQUIRE(worker.manualOverrideActive());
  std::this_thread::sleep_for(35ms);
  OMNI_REQUIRE(worker.enqueueAutomatic({CameraCommandType::ContinuousMove, -0.4F, 0.0F, 0.0F}));
  OMNI_REQUIRE(worker.enqueueAutomatic({CameraCommandType::Stop}));
  OMNI_REQUIRE(waitForControl([&worker] { return worker.metrics().commandsExecuted >= 2; }));
  const auto metrics = worker.metrics();
  worker.stop();
  OMNI_REQUIRE(metrics.queueDepth <= config.queueSize);
  OMNI_REQUIRE(metrics.commandsQueued >= 3);
}

OMNI_TEST(AutoPtzUsesTrackedTargetDeadZoneAndLostTimeout) {
  EventEngine events;
  auto state = std::make_shared<ControlState>();
  CameraControlConfig workerConfig;
  workerConfig.maximumCommandsPerSecond = 1000.0;
  CameraControlWorker worker("ptz", workerConfig, std::make_unique<MockCameraControl>(state), events);
  std::string error;
  OMNI_REQUIRE(worker.start(error));
  AutoPtzConfig config;
  config.enabled = true;
  config.deadZone = 0.10F;
  config.panTiltGain = 0.5F;
  config.maximumSpeed = 0.5F;
  config.commandInterval = 1ms;
  config.targetHold = 1ms;
  config.lostTimeout = 5ms;
  AutoPtzController controller("ptz", config, worker);
  DetectionResult result;
  result.frameId = 1;
  result.sourceId = "ptz";
  result.detections.push_back({0, "person", 0.9F, {80, 40, 10, 10}, 11U, 1, false});
  const auto acquired = controller.update(ptzFrame(), result);
  OMNI_REQUIRE(acquired.size() == 1 && acquired.front().type == EventType::TargetAcquired);
  OMNI_REQUIRE(waitForControl([&state] {
    const std::scoped_lock lock(state->mutex);
    return !state->commands.empty();
  }));
  {
    const std::scoped_lock lock(state->mutex);
    OMNI_REQUIRE(state->commands.front().type == CameraCommandType::ContinuousMove);
    OMNI_REQUIRE(state->commands.front().pan > 0.0F);
  }
  std::this_thread::sleep_for(8ms);
  DetectionResult absent;
  absent.frameId = 2;
  absent.sourceId = "ptz";
  absent.status = DetectionStatus::NoDetections;
  const auto lost = controller.update(ptzFrame(), absent);
  OMNI_REQUIRE(lost.size() == 1 && lost.front().type == EventType::TargetLost);
  worker.stop();
}

}  // namespace
