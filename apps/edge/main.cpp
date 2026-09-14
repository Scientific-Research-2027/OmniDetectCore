#include "application/DetectionPipeline.h"
#include "config/ConfigManager.h"
#include "core/inference/BackendFactory.h"
#include "core/logging/Logger.h"
#include "core/yolo/Yolo26Detector.h"
#include "input/SourceFactory.h"
#include "output/ImageSink.h"
#include "output/RecorderSink.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

std::atomic<bool> interrupted{false};

extern "C" void handleSignal(int) { interrupted.store(true); }

class ConsoleSink final : public omnidetect::IResultSink {
 public:
  void consume(const omnidetect::Frame&, const omnidetect::DetectionResult& result) noexcept override {
    if (!result.ok()) {
      std::cerr << "frame=" << result.frameId << " error=" << result.error << '\n';
      return;
    }
    std::cout << "frame=" << result.frameId << " detections=" << result.detections.size()
              << " preprocess_ms=" << static_cast<double>(result.preprocessTime.count()) / 1000.0
              << " inference_ms=" << static_cast<double>(result.inferenceTime.count()) / 1000.0
              << " total_ms=" << static_cast<double>(result.totalLatency.count()) / 1000.0 << '\n';
  }
  [[nodiscard]] std::string name() const override { return "console"; }
};

void usage(const char* executable) {
  std::cout << "Usage: " << executable << " [--config path/to/config.yaml]\n";
}

omnidetect::log::Level logLevel(const std::string& value) {
  if (value == "trace") return omnidetect::log::Level::Trace;
  if (value == "debug") return omnidetect::log::Level::Debug;
  if (value == "warning") return omnidetect::log::Level::Warning;
  if (value == "error") return omnidetect::log::Level::Error;
  if (value == "critical") return omnidetect::log::Level::Critical;
  return omnidetect::log::Level::Info;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path configPath = "config/config.yaml";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--config" && index + 1 < argc) configPath = argv[++index];
    else if (argument == "--help" || argument == "-h") { usage(argv[0]); return 0; }
    else { std::cerr << "Unknown argument: " << argument << '\n'; usage(argv[0]); return 2; }
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  omnidetect::log::info("OmniDetectCore edge 0.1.0 starting");

  const auto loaded = omnidetect::ConfigManager::load(configPath);
  if (!loaded.ok()) {
    std::cerr << "Configuration error: " << loaded.error << '\n';
    return 2;
  }
  auto config = loaded.config;
  omnidetect::log::setLevel(logLevel(config.logLevel));
  auto backend = omnidetect::BackendFactory::instance().create(config.backend);
  if (!backend) {
    std::cerr << "Unknown inference backend: " << config.backend << '\n';
    return 3;
  }
  auto detector = std::make_unique<omnidetect::Yolo26Detector>(config.detector, std::move(backend), config.backendConfig);
  if (!detector->isReady()) {
    std::cerr << "Detector initialization failed: " << detector->initializationError() << '\n';
    return 3;
  }
  std::string error;
  auto source = omnidetect::createFrameSource(config.source, error);
  if (!source) {
    std::cerr << "Source configuration failed: " << error << '\n';
    return 4;
  }

  omnidetect::DetectionPipeline pipeline(std::move(source), std::move(detector), config.pipeline,
                                         config.tracker, config.monitor);
  if (!pipeline.addSink(std::make_shared<ConsoleSink>(), error)) {
    std::cerr << "Cannot configure console output: " << error << '\n';
    return 5;
  }
  if (!config.output.imagePath.empty() &&
      !pipeline.addSink(std::make_shared<omnidetect::ImageSink>(config.output.imagePath,
                                                                config.source.type != omnidetect::SourceType::Image), error)) {
    std::cerr << "Cannot configure image output: " << error << '\n';
    return 5;
  }
  if (config.output.recorder &&
      !pipeline.addSink(std::make_shared<omnidetect::RecorderSink>(
                            omnidetect::RecorderConfig{config.output.recordingPath,
                                                       config.source.fps > 0.0 ? config.source.fps : 25.0,
                                                       2ULL * 1024ULL * 1024ULL * 1024ULL}), error)) {
    std::cerr << "Cannot configure recorder: " << error << '\n';
    return 5;
  }
  pipeline.events().subscribe([](const omnidetect::Event& event) {
    std::cout << "event=" << static_cast<int>(event.type) << " source=" << event.sourceId
              << " message=" << event.message << '\n';
  });

  if (!pipeline.start(error)) {
    std::cerr << "Pipeline start failed: " << error << '\n';
    return 6;
  }
  while (pipeline.isRunning() && !interrupted.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  pipeline.stop();
  const auto metrics = pipeline.metrics();
  std::cout << std::fixed << std::setprecision(2)
            << "summary captured=" << metrics.capturedFrames << " inferred=" << metrics.inferredFrames
            << " delivered=" << metrics.deliveredResults << " dropped_frames=" << metrics.droppedFrames
            << " capture_fps=" << metrics.captureFps << " inference_fps=" << metrics.inferenceFps
            << " avg_latency_ms=" << metrics.averageLatencyMs << '\n';
  return 0;
}
