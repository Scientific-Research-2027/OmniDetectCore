#pragma once

#include "application/LatestValueQueue.h"
#include "config/ConfigManager.h"
#include "core/detection/IObjectDetector.h"
#include "core/monitoring/EventEngine.h"
#include "core/monitoring/ObjectMonitor.h"
#include "core/tracking/ObjectTracker.h"
#include "input/IFrameSource.h"
#include "output/IResultSink.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace omnidetect {

struct PipelineMetrics {
  std::uint64_t capturedFrames{0};
  std::uint64_t inferredFrames{0};
  std::uint64_t deliveredResults{0};
  std::uint64_t droppedFrames{0};
  std::uint64_t droppedResults{0};
  double captureFps{0.0};
  double inferenceFps{0.0};
  double averageLatencyMs{0.0};
};

class DetectionPipeline {
 public:
  DetectionPipeline(std::unique_ptr<IFrameSource> source,
                    std::unique_ptr<IObjectDetector> detector,
                    PipelineConfig pipelineConfig = {},
                    TrackerConfig trackerConfig = {},
                    MonitorConfig monitorConfig = {});
  ~DetectionPipeline();

  DetectionPipeline(const DetectionPipeline&) = delete;
  DetectionPipeline& operator=(const DetectionPipeline&) = delete;

  bool addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept;
  bool start(std::string& error) noexcept;
  void stop() noexcept;
  bool restart(std::string& error) noexcept;
  [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
  [[nodiscard]] PipelineMetrics metrics() const noexcept;
  [[nodiscard]] EventEngine& events() noexcept { return eventEngine_; }

 private:
  struct ResultPacket {
    Frame frame;
    DetectionResult result;
  };

  std::unique_ptr<IFrameSource> source_;
  std::unique_ptr<IObjectDetector> detector_;
  PipelineConfig config_;
  ObjectTracker tracker_;
  ObjectMonitor monitor_;
  EventEngine eventEngine_;
  LatestValueQueue<Frame> frameQueue_;
  LatestValueQueue<ResultPacket> resultQueue_;

  mutable std::mutex lifecycleMutex_;
  mutable std::mutex sinksMutex_;
  std::vector<std::shared_ptr<IResultSink>> sinks_;
  std::thread captureThread_;
  std::thread inferenceThread_;
  std::thread deliveryThread_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> capturedFrames_{0};
  std::atomic<std::uint64_t> inferredFrames_{0};
  std::atomic<std::uint64_t> deliveredResults_{0};
  std::atomic<std::uint64_t> droppedFrames_{0};
  std::atomic<std::uint64_t> droppedResults_{0};
  std::atomic<std::uint64_t> totalLatencyMicroseconds_{0};
  std::chrono::steady_clock::time_point startedAt_{};

  void captureLoop() noexcept;
  void inferenceLoop() noexcept;
  void deliveryLoop() noexcept;
  void publishSourceEvent(EventType type, const std::string& message) noexcept;
};

}  // namespace omnidetect

