#include "tests/TestHarness.h"

#include "application/EdgeMonitoringService.h"
#include "core/monitoring/MultiObjectMonitor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using namespace omnidetect;
using namespace std::chrono_literals;

Frame cameraFrame(const std::string& sourceId, const std::uint64_t frameId,
                  const std::chrono::milliseconds age = 0ms) {
  auto image = std::make_shared<ImageBuffer>();
  image->width = 32;
  image->height = 24;
  image->channels = 3;
  image->stride = 96;
  image->format = PixelFormat::Bgr8;
  image->bytes.assign(image->stride * 24U, 0);
  return {std::move(image), frameId, std::chrono::steady_clock::now() - age,
          std::chrono::system_clock::now(), sourceId, {}};
}

class ContinuousSource final : public IFrameSource {
 public:
  ContinuousSource(std::string id, const bool stale = false, const unsigned failedOpens = 0)
      : id_(std::move(id)), stale_(stale), failedOpens_(failedOpens) {}

  bool open(std::string& error) noexcept override {
    ++openAttempts_;
    if (openAttempts_ <= failedOpens_) {
      error = "simulated connection failure";
      return false;
    }
    opened_.store(true);
    error.clear();
    return true;
  }
  FrameReadResult read() noexcept override {
    if (!opened_.load()) return {FrameReadStatus::FatalError, {}, "closed"};
    return {FrameReadStatus::Ready, cameraFrame(id_, nextFrame_++, stale_ ? 2s : 0ms), {}};
  }
  void close() noexcept override { opened_.store(false); }
  [[nodiscard]] bool isOpened() const noexcept override { return opened_.load(); }
  [[nodiscard]] SourceMetadata metadata() const override { return {id_, "mock", 32, 24, 200.0}; }

 private:
  std::string id_;
  bool stale_{false};
  unsigned failedOpens_{0};
  unsigned openAttempts_{0};
  std::uint64_t nextFrame_{1};
  std::atomic<bool> opened_{false};
};

struct DetectorStats {
  std::atomic<int> instances{0};
  std::atomic<int> calls{0};
  std::atomic<int> concurrent{0};
  std::atomic<int> maximumConcurrent{0};
};

class SharedMockDetector final : public IObjectDetector {
 public:
  explicit SharedMockDetector(std::shared_ptr<DetectorStats> stats) : stats_(std::move(stats)) {
    ++stats_->instances;
  }
  [[nodiscard]] bool isReady() const noexcept override { return true; }
  [[nodiscard]] std::string name() const override { return "shared-mock"; }
  DetectionResult detect(const Frame& frame) noexcept override {
    ++stats_->calls;
    const auto active = ++stats_->concurrent;
    auto observed = stats_->maximumConcurrent.load();
    while (active > observed && !stats_->maximumConcurrent.compare_exchange_weak(observed, active)) {}
    std::this_thread::sleep_for(1ms);
    --stats_->concurrent;
    DetectionResult result;
    result.frameId = frame.frameId;
    result.sourceId = frame.sourceId;
    result.status = DetectionStatus::NoDetections;
    result.totalLatency = 1ms;
    return result;
  }

 private:
  std::shared_ptr<DetectorStats> stats_;
};

class PerCameraSink final : public IResultSink {
 public:
  void consume(const Frame& frame, const DetectionResult& result) noexcept override {
    const std::scoped_lock lock(mutex_);
    ++counts_[result.sourceId];
    if (frame.sourceId != result.sourceId) routingValid_ = false;
  }
  [[nodiscard]] std::string name() const override { return "per-camera-counter"; }
  [[nodiscard]] std::unordered_map<std::string, std::uint64_t> counts() const {
    const std::scoped_lock lock(mutex_);
    return counts_;
  }
  [[nodiscard]] bool routingValid() const {
    const std::scoped_lock lock(mutex_);
    return routingValid_;
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::uint64_t> counts_;
  bool routingValid_{true};
};

AppConfig edgeConfig(const std::vector<std::string>& ids) {
  AppConfig config;
  config.sharedInference.maxFrameAge = 100ms;
  for (const auto& id : ids) {
    CameraConfig camera;
    camera.id = id;
    camera.source.sourceId = id;
    camera.source.captureFps = 200.0;
    camera.source.inferenceFps = 200.0;
    camera.frameQueueSize = 1;
    camera.reconnectInitialDelay = 5ms;
    camera.reconnectMaximumDelay = 20ms;
    camera.trackingEnabled = false;
    camera.monitoringEnabled = false;
    config.cameras.push_back(std::move(camera));
  }
  return config;
}

template <typename Predicate>
bool waitFor(Predicate predicate, const std::chrono::milliseconds timeout = 2000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

OMNI_TEST(FourCamerasShareOneDetectorAndReceiveFairService) {
  auto config = edgeConfig({"cam-a", "cam-b", "cam-c", "cam-d"});
  auto detectorStats = std::make_shared<DetectorStats>();
  auto sink = std::make_shared<PerCameraSink>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId);
      });
  OMNI_REQUIRE(service.isReady());
  std::string error;
  OMNI_REQUIRE(service.addSink(sink, error));
  OMNI_REQUIRE(service.start(error));
  OMNI_REQUIRE(waitFor([&sink] {
    const auto counts = sink->counts();
    return counts.size() == 4 && std::all_of(counts.begin(), counts.end(), [](const auto& item) {
      return item.second >= 8;
    });
  }));
  service.stop();
  const auto counts = sink->counts();
  std::uint64_t minimum = counts.begin()->second;
  std::uint64_t maximum = minimum;
  for (const auto& [unused, count] : counts) {
    static_cast<void>(unused);
    minimum = std::min(minimum, count);
    maximum = std::max(maximum, count);
  }
  OMNI_REQUIRE(detectorStats->instances.load() == 1);
  OMNI_REQUIRE(detectorStats->maximumConcurrent.load() == 1);
  OMNI_REQUIRE(maximum <= minimum * 2U + 2U);
  OMNI_REQUIRE(sink->routingValid());
  const auto beforeRestart = counts.at("cam-a");
  OMNI_REQUIRE(service.start(error));
  OMNI_REQUIRE(waitFor([&sink, beforeRestart] {
    const auto restarted = sink->counts();
    const auto found = restarted.find("cam-a");
    return found != restarted.end() && found->second > beforeRestart;
  }));
  service.stop();
}

OMNI_TEST(HighFpsCameraDoesNotStarveLowFpsCamera) {
  auto config = edgeConfig({"fast", "slow"});
  config.cameras[0].source.captureFps = 500.0;
  config.cameras[0].source.inferenceFps = 500.0;
  config.cameras[1].source.captureFps = 40.0;
  config.cameras[1].source.inferenceFps = 40.0;
  auto detectorStats = std::make_shared<DetectorStats>();
  auto sink = std::make_shared<PerCameraSink>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId);
      });
  std::string error;
  OMNI_REQUIRE(service.addSink(sink, error));
  OMNI_REQUIRE(service.start(error));
  OMNI_REQUIRE(waitFor([&sink] {
    const auto counts = sink->counts();
    const auto slow = counts.find("slow");
    return slow != counts.end() && slow->second >= 5;
  }));
  service.stop();
  const auto counts = sink->counts();
  OMNI_REQUIRE(counts.contains("fast") && counts.at("fast") > 0);
  OMNI_REQUIRE(counts.contains("slow") && counts.at("slow") >= 5);
}

OMNI_TEST(CameraCanBeAddedAndRemovedWhileServiceIsRunning) {
  auto config = edgeConfig({"primary"});
  auto detectorStats = std::make_shared<DetectorStats>();
  auto sink = std::make_shared<PerCameraSink>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId);
      });
  std::string error;
  OMNI_REQUIRE(service.addSink(sink, error));
  OMNI_REQUIRE(service.start(error));
  auto added = edgeConfig({"added"}).cameras.front();
  OMNI_REQUIRE(service.addCamera(added, error));
  OMNI_REQUIRE(waitFor([&sink] {
    const auto counts = sink->counts();
    const auto found = counts.find("added");
    return found != counts.end() && found->second >= 3;
  }));
  OMNI_REQUIRE(service.removeCamera("added", error));
  const auto metrics = service.metrics();
  OMNI_REQUIRE(metrics.camerasConfigured == 1);
  OMNI_REQUIRE(metrics.scheduler.registeredCameras == 1);
  service.stop();
}

OMNI_TEST(CameraReconnectDoesNotStopHealthyCamera) {
  auto config = edgeConfig({"healthy", "recovering"});
  auto detectorStats = std::make_shared<DetectorStats>();
  auto sink = std::make_shared<PerCameraSink>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId, false,
                                                  source.sourceId == "recovering" ? 2U : 0U);
      });
  std::string error;
  OMNI_REQUIRE(service.addSink(sink, error));
  OMNI_REQUIRE(service.start(error));
  OMNI_REQUIRE(waitFor([&sink] {
    const auto counts = sink->counts();
    const auto healthy = counts.find("healthy");
    const auto recovered = counts.find("recovering");
    return healthy != counts.end() && healthy->second >= 5 && recovered != counts.end() && recovered->second >= 3;
  }));
  const auto metrics = service.metrics();
  service.stop();
  const auto recovering = std::find_if(metrics.cameras.begin(), metrics.cameras.end(), [](const auto& camera) {
    return camera.cameraId == "recovering";
  });
  OMNI_REQUIRE(recovering != metrics.cameras.end());
  OMNI_REQUIRE(recovering->reconnectCount >= 2);
}

OMNI_TEST(FailFastPolicyRejectsAnInitialCameraConnectionFailure) {
  auto config = edgeConfig({"healthy", "offline"});
  config.failFast = true;
  for (auto& camera : config.cameras) camera.startupTimeout = 100ms;
  auto detectorStats = std::make_shared<DetectorStats>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId, false,
                                                  source.sourceId == "offline" ? 1000U : 0U);
      });
  std::string error;
  OMNI_REQUIRE(!service.start(error));
  OMNI_REQUIRE(error.find("simulated connection failure") != std::string::npos);
  OMNI_REQUIRE(!service.isRunning());
}

OMNI_TEST(StaleFramesAreDiscardedBeforeSharedInference) {
  auto config = edgeConfig({"stale"});
  config.sharedInference.maxFrameAge = 10ms;
  auto detectorStats = std::make_shared<DetectorStats>();
  EdgeMonitoringService service(
      config, std::make_unique<SharedMockDetector>(detectorStats),
      [](const SourceConfig& source, std::string& error) {
        error.clear();
        return std::make_unique<ContinuousSource>(source.sourceId, true);
      });
  std::string error;
  OMNI_REQUIRE(service.start(error));
  OMNI_REQUIRE(waitFor([&service] {
    const auto metrics = service.metrics();
    return !metrics.cameras.empty() && metrics.cameras.front().framesStale >= 3;
  }));
  service.stop();
  OMNI_REQUIRE(detectorStats->calls.load() == 0);
}

OMNI_TEST(MultiObjectMonitorSeparatesCamerasWithTheSameTrackId) {
  MonitorConfig config;
  config.confirmationFrames = 2;
  config.lostFrames = 2;
  MultiObjectMonitor monitor(config);
  Detection detection{0, "person", 0.9F, {1, 1, 5, 5}, 7U, 1, false};
  DetectionResult cameraA;
  cameraA.sourceId = "a";
  cameraA.detections = {detection};
  DetectionResult cameraB = cameraA;
  cameraB.sourceId = "b";
  static_cast<void>(monitor.update(cameraA));
  static_cast<void>(monitor.update(cameraA));
  static_cast<void>(monitor.update(cameraB));
  static_cast<void>(monitor.update(cameraB));
  OMNI_REQUIRE(monitor.snapshot().size() == 2);
  DetectionResult absent;
  absent.sourceId = "a";
  absent.status = DetectionStatus::NoDetections;
  OMNI_REQUIRE(monitor.update(absent).empty());
  const auto events = monitor.update(absent);
  OMNI_REQUIRE(events.size() == 1 && events.front().type == EventType::ObjectLost);
  const auto remaining = monitor.snapshot();
  OMNI_REQUIRE(remaining.size() == 1 && remaining.front().cameraId == "b");
}

OMNI_TEST(MultiObjectMonitorTracksTwoObjectsInOneCameraIndependently) {
  MonitorConfig config;
  config.confirmationFrames = 1;
  config.lostFrames = 1;
  MultiObjectMonitor monitor(config);
  DetectionResult result;
  result.sourceId = "camera";
  result.detections = {
      {0, "person", 0.9F, {1, 1, 5, 5}, 1U, 1, false},
      {0, "person", 0.8F, {10, 1, 5, 5}, 2U, 1, false},
  };
  const auto detected = monitor.update(result);
  OMNI_REQUIRE(detected.size() == 4);
  OMNI_REQUIRE(monitor.snapshot().size() == 2);
  DetectionResult oneLeft = result;
  oneLeft.detections.erase(oneLeft.detections.begin());
  const auto lost = monitor.update(oneLeft);
  OMNI_REQUIRE(lost.size() == 1 && lost.front().detection->trackId == 1U);
  const auto remaining = monitor.snapshot();
  OMNI_REQUIRE(remaining.size() == 1 && remaining.front().trackId == 2U);
}

OMNI_TEST(ConfigParserLoadsAndValidatesMultipleCameras) {
  const auto directory = std::filesystem::temp_directory_path();
  const auto path = directory / "omnidetect_multicamera_test.yaml";
  const auto model = directory / "omnidetect_multicamera_test.onnx";
  {
    std::ofstream modelFile(model, std::ios::binary);
    modelFile << "model";
    std::ofstream configFile(path);
    configFile << "model:\n  path: omnidetect_multicamera_test.onnx\n"
                  "inference:\n  backend: onnx\n  worker_count: 1\n  scheduler: round_robin\n"
                  "  max_frame_age_ms: 200\n"
                  "cameras:\n"
                  "  - id: entrance\n    source:\n      type: camera_id\n      capture_fps: 20\n"
                  "      inference_fps: 4\n      queue_size: 1\n"
                  "  - id: loading-bay\n    source:\n      type: camera_id\n      capture_fps: 15\n"
                  "      inference_fps: 3\n    monitoring:\n      target_classes: [person, car]\n";
  }
  const auto loaded = ConfigManager::load(path);
  std::filesystem::remove(path);
  std::filesystem::remove(model);
  OMNI_REQUIRE(loaded.ok());
  OMNI_REQUIRE(loaded.config.cameras.size() == 2);
  OMNI_REQUIRE(loaded.config.cameras[1].monitor.targetClassNames.contains("car"));
  auto invalid = loaded.config;
  invalid.cameras[1].id = invalid.cameras[0].id;
  std::string error;
  OMNI_REQUIRE(!ConfigManager::validate(invalid, error));
  invalid = loaded.config;
  invalid.cameras[0].source.inferenceFps = 0.0;
  OMNI_REQUIRE(!ConfigManager::validate(invalid, error));
}

OMNI_TEST(ConfigParserFailsClearlyWhenCameraSecretEnvironmentIsMissing) {
  const auto directory = std::filesystem::temp_directory_path();
  const auto path = directory / "omnidetect_missing_env_test.yaml";
  const auto model = directory / "omnidetect_missing_env_test.onnx";
  {
    std::ofstream modelFile(model, std::ios::binary);
    modelFile << "model";
    std::ofstream configFile(path);
    configFile << "model:\n  path: omnidetect_missing_env_test.onnx\n"
                  "cameras:\n  - id: secure\n    source:\n      type: rtsp\n"
                  "      uri: ${OMNIDETECT_TEST_SECRET_THAT_MUST_NOT_EXIST_20260915}\n"
                  "      inference_fps: 1\n";
  }
  const auto loaded = ConfigManager::load(path);
  std::filesystem::remove(path);
  std::filesystem::remove(model);
  OMNI_REQUIRE(!loaded.ok());
  OMNI_REQUIRE(loaded.error.find("OMNIDETECT_TEST_SECRET_THAT_MUST_NOT_EXIST_20260915") != std::string::npos);
  OMNI_REQUIRE(loaded.error.find("rtsp://") == std::string::npos);
}

}  // namespace
