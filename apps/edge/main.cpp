#include "application/DetectionPipeline.h"
#include "application/EdgeMonitoringService.h"
#include "config/ConfigManager.h"
#include "core/inference/BackendFactory.h"
#include "core/logging/Logger.h"
#include "core/yolo/Yolo26Detector.h"
#include "input/SourceFactory.h"
#include "output/ImageSink.h"
#include "output/RecorderSink.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
      std::cerr << "camera=" << result.sourceId << " frame=" << result.frameId
                << " error=" << result.error << '\n';
      return;
    }
    std::cout << "camera=" << result.sourceId << " frame=" << result.frameId
              << " detections=" << result.detections.size()
              << " preprocess_ms=" << static_cast<double>(result.preprocessTime.count()) / 1000.0
              << " inference_ms=" << static_cast<double>(result.inferenceTime.count()) / 1000.0
              << " total_ms=" << static_cast<double>(result.totalLatency.count()) / 1000.0 << '\n';
  }
  [[nodiscard]] std::string name() const override { return "console"; }
};

void usage(const char* executable) {
  std::cout << "Usage: " << executable
            << " [--config path/to/config.yaml] [--metrics-interval-seconds N]\n";
}

omnidetect::log::Level logLevel(const std::string& value) {
  if (value == "trace") return omnidetect::log::Level::Trace;
  if (value == "debug") return omnidetect::log::Level::Debug;
  if (value == "warning") return omnidetect::log::Level::Warning;
  if (value == "error") return omnidetect::log::Level::Error;
  if (value == "critical") return omnidetect::log::Level::Critical;
  return omnidetect::log::Level::Info;
}

void printEvent(const omnidetect::Event& event) {
  std::cout << "event=" << static_cast<int>(event.type) << " camera=" << event.sourceId
            << " message=" << event.message << '\n';
}

int runMultiCamera(omnidetect::AppConfig config,
                   std::unique_ptr<omnidetect::IObjectDetector> detector,
                   const double metricsIntervalSeconds) {
  omnidetect::EdgeMonitoringService service(config, std::move(detector));
  if (!service.isReady()) {
    std::cerr << "Multi-camera service initialization failed: " << service.initializationError() << '\n';
    return 4;
  }
  std::string error;
  if (!service.addSink(std::make_shared<ConsoleSink>(), error)) {
    std::cerr << "Cannot configure console output: " << error << '\n';
    return 5;
  }
  if (!config.output.imagePath.empty() &&
      !service.addSink(std::make_shared<omnidetect::ImageSink>(config.output.imagePath, true), error)) {
    std::cerr << "Cannot configure image output: " << error << '\n';
    return 5;
  }
  if (config.output.recorder) {
    double recordingFps = 1.0;
    for (const auto& camera : config.cameras) recordingFps = std::max(recordingFps, camera.source.inferenceFps);
    if (!service.addSink(std::make_shared<omnidetect::RecorderSink>(
                             omnidetect::RecorderConfig{config.output.recordingPath, recordingFps,
                                                        2ULL * 1024ULL * 1024ULL * 1024ULL}), error)) {
      std::cerr << "Cannot configure recorder: " << error << '\n';
      return 5;
    }
  }
  service.events().subscribe(printEvent);
  if (!service.start(error)) {
    std::cerr << "Multi-camera service start failed: " << error << '\n';
    return 6;
  }
  auto nextMetrics = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(metricsIntervalSeconds));
  while (service.isRunning() && !interrupted.load()) {
    const auto metrics = service.metrics();
    const auto active = std::any_of(metrics.cameras.begin(), metrics.cameras.end(), [](const auto& camera) {
      return camera.state != omnidetect::CameraSessionState::Stopped &&
             camera.state != omnidetect::CameraSessionState::Faulted;
    });
    if (!active) break;
    if (metricsIntervalSeconds > 0.0 && std::chrono::steady_clock::now() >= nextMetrics) {
      std::cout << std::fixed << std::setprecision(2)
                << "health uptime=" << metrics.uptimeSeconds
                << " inference_fps=" << metrics.inference.inferenceFps
                << " p95_latency_ms=" << metrics.inference.p95LatencyMs
                << " utilization=" << metrics.inference.utilization
                << " router_dropped=" << metrics.router.dropped << '\n';
      for (const auto& camera : metrics.cameras) {
        std::cout << "camera_health uptime=" << metrics.uptimeSeconds << " id=" << camera.cameraId
                  << " capture_fps=" << camera.captureFps << " inference_fps=" << camera.inferenceFps
                  << " dropped=" << camera.framesDropped << " skipped=" << camera.framesSkipped
                  << " stale=" << camera.framesStale << " reconnects=" << camera.reconnectCount
                  << " last_inference_age_ms=" << camera.lastInferenceAgeMs << '\n';
      }
      nextMetrics = std::chrono::steady_clock::now() +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(metricsIntervalSeconds));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  service.stop();
  const auto metrics = service.metrics();
  std::cout << std::fixed << std::setprecision(2)
            << "summary cameras=" << metrics.camerasConfigured
            << " inferred=" << metrics.inference.framesInferred
            << " delivered=" << metrics.router.delivered
            << " dropped_results=" << metrics.router.dropped
            << " inference_fps=" << metrics.inference.inferenceFps
            << " avg_latency_ms=" << metrics.inference.averageLatencyMs
            << " p95_latency_ms=" << metrics.inference.p95LatencyMs << '\n';
  for (const auto& camera : metrics.cameras) {
    std::cout << "camera_summary id=" << camera.cameraId << " captured=" << camera.framesCaptured
              << " inferred=" << camera.framesInferred << " dropped=" << camera.framesDropped
              << " skipped=" << camera.framesSkipped << " stale=" << camera.framesStale
              << " reconnects=" << camera.reconnectCount << '\n';
  }
  return 0;
}

int runLegacySingleCamera(omnidetect::AppConfig config,
                          std::unique_ptr<omnidetect::IObjectDetector> detector) {
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
      !pipeline.addSink(std::make_shared<omnidetect::ImageSink>(
                            config.output.imagePath, config.source.type != omnidetect::SourceType::Image), error)) {
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
  pipeline.events().subscribe(printEvent);
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

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path configPath = "config/config.yaml";
  double metricsIntervalSeconds = 0.0;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--config" && index + 1 < argc) configPath = argv[++index];
    else if (argument == "--metrics-interval-seconds" && index + 1 < argc) {
      try {
        metricsIntervalSeconds = std::stod(argv[++index]);
      } catch (...) {
        std::cerr << "Invalid metrics interval\n";
        return 2;
      }
      if (!std::isfinite(metricsIntervalSeconds) || metricsIntervalSeconds < 0.0 ||
          metricsIntervalSeconds > 86400.0) {
        std::cerr << "Metrics interval must be in [0,86400] seconds\n";
        return 2;
      }
    }
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
  if (!config.cameras.empty()) {
    return runMultiCamera(std::move(config), std::move(detector), metricsIntervalSeconds);
  }
  return runLegacySingleCamera(std::move(config), std::move(detector));
}
