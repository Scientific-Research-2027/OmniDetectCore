#pragma once

#include "application/InferenceScheduler.h"
#include "application/ResultRouter.h"
#include "config/ConfigManager.h"
#include "core/detection/IObjectDetector.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace omnidetect {

struct InferenceServiceMetrics {
  std::uint64_t framesInferred{0};
  std::uint64_t inferenceErrors{0};
  std::uint64_t resultsRejected{0};
  double inferenceFps{0.0};
  double averageLatencyMs{0.0};
  double p95LatencyMs{0.0};
  double utilization{0.0};
  bool modelReady{false};
  bool running{false};
};

class InferenceService {
 public:
  InferenceService(std::unique_ptr<IObjectDetector> detector,
                   std::shared_ptr<IInferenceScheduler> scheduler,
                   ResultRouter& router,
                   SharedInferenceConfig config);
  ~InferenceService();

  InferenceService(const InferenceService&) = delete;
  InferenceService& operator=(const InferenceService&) = delete;

  bool start(std::string& error) noexcept;
  void stop() noexcept;
  [[nodiscard]] bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }
  [[nodiscard]] InferenceServiceMetrics metrics() const noexcept;

 private:
  std::unique_ptr<IObjectDetector> detector_;
  std::shared_ptr<IInferenceScheduler> scheduler_;
  ResultRouter& router_;
  SharedInferenceConfig config_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  mutable std::mutex lifecycleMutex_;
  mutable std::mutex waitMutex_;
  std::condition_variable waitCondition_;
  mutable std::mutex metricsMutex_;
  std::chrono::steady_clock::time_point startedAt_{};
  std::chrono::microseconds busyTime_{0};
  std::uint64_t framesInferred_{0};
  std::uint64_t inferenceErrors_{0};
  std::uint64_t resultsRejected_{0};
  std::deque<double> latencySamplesMs_;

  void run() noexcept;
};

}  // namespace omnidetect

