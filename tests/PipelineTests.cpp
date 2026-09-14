#include "tests/TestHarness.h"

#include "application/DetectionPipeline.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

using namespace omnidetect;

Frame mockFrame(const std::uint64_t id) {
  auto image = std::make_shared<ImageBuffer>();
  image->width = 8;
  image->height = 8;
  image->channels = 3;
  image->stride = 24;
  image->format = PixelFormat::Bgr8;
  image->bytes.assign(192, 0);
  return {std::move(image), id, std::chrono::steady_clock::now(), std::chrono::system_clock::now(), "mock", {}};
}

class MockSource final : public IFrameSource {
 public:
  bool open(std::string& error) noexcept override { error.clear(); next_ = 1; opened_ = true; return true; }
  FrameReadResult read() noexcept override {
    if (!opened_) return {FrameReadStatus::FatalError, {}, "closed"};
    if (next_ > 20) return {FrameReadStatus::EndOfStream, {}, {}};
    return {FrameReadStatus::Ready, mockFrame(next_++), {}};
  }
  void close() noexcept override { opened_ = false; }
  [[nodiscard]] bool isOpened() const noexcept override { return opened_; }
  [[nodiscard]] SourceMetadata metadata() const override { return {"mock", "mock", 8, 8, 0.0}; }
 private:
  std::uint64_t next_{1};
  bool opened_{false};
};

class MockDetector final : public IObjectDetector {
 public:
  [[nodiscard]] bool isReady() const noexcept override { return true; }
  [[nodiscard]] std::string name() const override { return "mock"; }
  DetectionResult detect(const Frame& frame) noexcept override {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    DetectionResult result;
    result.frameId = frame.frameId;
    result.sourceId = frame.sourceId;
    result.status = DetectionStatus::NoDetections;
    result.totalLatency = std::chrono::microseconds(1000);
    return result;
  }
};

class CountingSink final : public IResultSink {
 public:
  bool start(std::string& error) noexcept override { error.clear(); ++starts; return true; }
  void consume(const Frame&, const DetectionResult&) noexcept override { ++results; }
  void stop() noexcept override { ++stops; }
  [[nodiscard]] std::string name() const override { return "counter"; }
  std::atomic<int> starts{0};
  std::atomic<int> stops{0};
  std::atomic<int> results{0};
};

bool waitUntilStopped(DetectionPipeline& pipeline) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (pipeline.isRunning() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return !pipeline.isRunning();
}

OMNI_TEST(PipelineStartsStopsAndRestartsWithBoundedQueues) {
  PipelineConfig config;
  config.frameQueueSize = 2;
  config.resultQueueSize = 2;
  config.dropOldFrames = true;
  config.trackingEnabled = false;
  config.monitoringEnabled = false;
  DetectionPipeline pipeline(std::make_unique<MockSource>(), std::make_unique<MockDetector>(), config);
  auto sink = std::make_shared<CountingSink>();
  std::string error;
  OMNI_REQUIRE(pipeline.addSink(sink, error));
  OMNI_REQUIRE(pipeline.start(error));
  OMNI_REQUIRE(waitUntilStopped(pipeline));
  pipeline.stop();
  OMNI_REQUIRE(sink->results.load() > 0);
  OMNI_REQUIRE(pipeline.metrics().droppedFrames > 0);
  const auto firstResults = sink->results.load();
  OMNI_REQUIRE(pipeline.restart(error));
  OMNI_REQUIRE(waitUntilStopped(pipeline));
  pipeline.stop();
  OMNI_REQUIRE(sink->results.load() > firstResults);
  OMNI_REQUIRE(sink->starts.load() == 2);
}

}  // namespace
