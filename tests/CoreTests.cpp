#include "tests/TestHarness.h"

#include "application/LatestValueQueue.h"
#include "config/ConfigManager.h"
#include "core/inference/BackendFactory.h"
#include "core/monitoring/EventEngine.h"
#include "core/monitoring/ObjectMonitor.h"
#include "core/preprocessing/Preprocessor.h"
#include "core/yolo/Yolo26Detector.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {

using namespace omnidetect;

Frame makeFrame(const int width = 100, const int height = 50) {
  auto image = std::make_shared<ImageBuffer>();
  image->width = width;
  image->height = height;
  image->channels = 3;
  image->stride = static_cast<std::size_t>(width * 3);
  image->format = PixelFormat::Bgr8;
  image->bytes.assign(image->stride * static_cast<std::size_t>(height), 127);
  return {std::move(image), 42, std::chrono::steady_clock::now(), std::chrono::system_clock::now(), "test", {}};
}

class SyntheticBackend final : public IInferenceBackend {
 public:
  explicit SyntheticBackend(Tensor output) : output_(std::move(output)) {}
  bool initialize(const BackendConfig&, std::string& error) noexcept override { error.clear(); ready_ = true; return true; }
  BackendResult infer(const Tensor&) noexcept override { return {true, {output_}, {}}; }
  void shutdown() noexcept override { ready_ = false; }
  [[nodiscard]] bool isReady() const noexcept override { return ready_; }
  [[nodiscard]] std::string name() const override { return "synthetic"; }
  [[nodiscard]] std::vector<TensorInfo> inputMetadata() const override { return {}; }
  [[nodiscard]] std::vector<TensorInfo> outputMetadata() const override { return {{"output", output_.shape}}; }
 private:
  Tensor output_;
  bool ready_{false};
};

OMNI_TEST(FrameCopyAndMoveShareImageOwnership) {
  auto frame = makeFrame();
  auto copy = frame;
  OMNI_REQUIRE(copy.image == frame.image);
  auto moved = std::move(copy);
  OMNI_REQUIRE(moved.valid());
  OMNI_REQUIRE(moved.width() == 100);
}

OMNI_TEST(LetterboxTransformMapsBackToOriginalPixels) {
  Preprocessor preprocessor({200, 200, true, 1.0F / 255.0F, 114.0F});
  const auto processed = preprocessor.process(makeFrame());
  OMNI_REQUIRE(processed.ok());
  OMNI_REQUIRE_NEAR(processed.transform.scale, 2.0, 0.001);
  OMNI_REQUIRE_NEAR(processed.transform.padY, 50.0, 0.001);
  const auto box = processed.transform.toOriginal({20.0F, 70.0F, 100.0F, 60.0F});
  OMNI_REQUIRE_NEAR(box.x, 10.0, 0.001);
  OMNI_REQUIRE_NEAR(box.y, 10.0, 0.001);
  OMNI_REQUIRE_NEAR(box.width, 50.0, 0.001);
  OMNI_REQUIRE_NEAR(box.height, 30.0, 0.001);
}

OMNI_TEST(YoloDecodeFiltersConfidenceClassAndNms) {
  Tensor output{{3, 6}, {
      10.0F, 60.0F, 110.0F, 120.0F, 0.90F, 0.0F,
      12.0F, 62.0F, 108.0F, 118.0F, 0.80F, 0.0F,
      20.0F, 70.0F, 60.0F, 100.0F, 0.20F, 1.0F}};
  DetectorConfig config;
  config.inputWidth = 200;
  config.inputHeight = 200;
  config.confidenceThreshold = 0.5F;
  config.iouThreshold = 0.5F;
  config.classNames = {"kite", "bird"};
  auto detector = Yolo26Detector(config, std::make_unique<SyntheticBackend>(output), {});
  OMNI_REQUIRE(detector.isReady());
  const auto result = detector.detect(makeFrame());
  OMNI_REQUIRE(result.ok());
  OMNI_REQUIRE(result.detections.size() == 1);
  OMNI_REQUIRE(result.detections.front().className == "kite");
  OMNI_REQUIRE_NEAR(result.detections.front().bbox.x, 5.0, 0.01);
  OMNI_REQUIRE_NEAR(result.detections.front().bbox.y, 5.0, 0.01);
}

OMNI_TEST(LatestQueueDropsOldValuesAtCapacity) {
  LatestValueQueue<int> queue(2, true);
  OMNI_REQUIRE(queue.push(1).dropped == 0);
  OMNI_REQUIRE(queue.push(2).dropped == 0);
  OMNI_REQUIRE(queue.push(3).dropped == 1);
  OMNI_REQUIRE(queue.pop().value() == 2);
  OMNI_REQUIRE(queue.pop().value() == 3);
  queue.close();
  OMNI_REQUIRE(!queue.pop().has_value());
}

OMNI_TEST(EventEngineUsesStableSnapshotDuringPublish) {
  EventEngine engine;
  std::atomic<int> calls{0};
  const auto id = engine.subscribe([&calls](const Event&) { ++calls; });
  OMNI_REQUIRE(id != 0);
  engine.publish({});
  OMNI_REQUIRE(calls.load() == 1);
  OMNI_REQUIRE(engine.unsubscribe(id));
  engine.publish({});
  OMNI_REQUIRE(calls.load() == 1);
}

OMNI_TEST(MonitorRequiresTemporalConfirmationAndEmitsLost) {
  MonitorConfig config;
  config.confirmationFrames = 2;
  config.lostFrames = 2;
  config.minimumConfidence = 0.5F;
  config.targetClassNames.insert("kite");
  ObjectMonitor monitor(config);
  DetectionResult present;
  present.status = DetectionStatus::Ok;
  present.sourceId = "test";
  present.detections.push_back({0, "kite", 0.9F, {0, 0, 10, 10}, {}, 0, false});
  auto events = monitor.update(present);
  OMNI_REQUIRE(events.size() == 1 && events.front().type == EventType::ObjectDetected);
  events = monitor.update(present);
  OMNI_REQUIRE(events.size() == 1 && events.front().type == EventType::ObjectConfirmed);
  DetectionResult absent;
  absent.status = DetectionStatus::NoDetections;
  OMNI_REQUIRE(monitor.update(absent).empty());
  events = monitor.update(absent);
  OMNI_REQUIRE(events.size() == 1 && events.front().type == EventType::ObjectLost);
}

OMNI_TEST(ConfigValidationRejectsUnsafeRanges) {
  AppConfig config;
  config.detector.confidenceThreshold = 1.5F;
  std::string error;
  OMNI_REQUIRE(!ConfigManager::validate(config, error));
  OMNI_REQUIRE(error.find("confidence") != std::string::npos);
}

OMNI_TEST(ConfigParserLoadsNestedYamlAndLists) {
  const auto path = std::filesystem::temp_directory_path() / "omnidetect_test_config.yaml";
  const auto modelPath = std::filesystem::temp_directory_path() / "omnidetect_test_model.onnx";
  {
    std::ofstream model(modelPath, std::ios::binary);
    model << "test";
    std::ofstream file(path);
    file << "model:\n  path: omnidetect_test_model.onnx\n  input_size: 320\n  confidence: 0.4\n"
            "inference:\n  backend: onnx\n  threads: 2\n"
            "source:\n  type: webcam\n  width: 640\n  height: 480\n"
            "monitoring:\n  target_classes:\n    - kite\n";
  }
  const auto loaded = ConfigManager::load(path);
  std::filesystem::remove(path);
  std::filesystem::remove(modelPath);
  OMNI_REQUIRE(loaded.ok());
  OMNI_REQUIRE(loaded.config.detector.inputWidth == 320);
  OMNI_REQUIRE(loaded.config.backend == "onnx");
  OMNI_REQUIRE(loaded.config.monitor.targetClassNames.contains("kite"));
}

OMNI_TEST(BackendFactoryReturnsKnownAdaptersAndRejectsUnknown) {
  OMNI_REQUIRE(BackendFactory::instance().create("ONNX") != nullptr);
  OMNI_REQUIRE(BackendFactory::instance().create("ncnn") != nullptr);
  OMNI_REQUIRE(BackendFactory::instance().create("does-not-exist") == nullptr);
}

}  // namespace
