#include "application/InferenceService.h"

#include "core/logging/Logger.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace omnidetect {
namespace {
constexpr std::size_t kLatencyWindow = 512;

double p95(std::deque<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1U;
  return values[std::min(index, values.size() - 1U)];
}
}  // namespace

InferenceService::InferenceService(std::unique_ptr<IObjectDetector> detector,
                                   std::shared_ptr<IInferenceScheduler> scheduler,
                                   ResultRouter& router,
                                   SharedInferenceConfig config)
    : detector_(std::move(detector)), scheduler_(std::move(scheduler)), router_(router), config_(std::move(config)) {}

InferenceService::~InferenceService() { stop(); }

bool InferenceService::start(std::string& error) noexcept {
  if (worker_.joinable()) stop();
  const std::scoped_lock lock(lifecycleMutex_);
  if (!detector_ || !detector_->isReady()) {
    error = "Shared inference detector is not ready";
    return false;
  }
  if (!scheduler_) {
    error = "Shared inference scheduler is not configured";
    return false;
  }
  {
    const std::scoped_lock metricsLock(metricsMutex_);
    startedAt_ = std::chrono::steady_clock::now();
    busyTime_ = std::chrono::microseconds(0);
    framesInferred_ = 0;
    inferenceErrors_ = 0;
    resultsRejected_ = 0;
    latencySamplesMs_.clear();
  }
  scheduler_->start();
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread(&InferenceService::run, this);
  } catch (const std::exception& exception) {
    running_.store(false, std::memory_order_release);
    scheduler_->stop();
    error = std::string("Cannot start shared inference worker: ") + exception.what();
    return false;
  }
  log::info("Shared inference service started with one worker and one " + detector_->name() + " instance");
  error.clear();
  return true;
}

void InferenceService::stop() noexcept {
  std::unique_lock lock(lifecycleMutex_);
  const bool wasActive = running_.exchange(false, std::memory_order_acq_rel) || worker_.joinable();
  if (!wasActive) return;
  if (scheduler_) scheduler_->stop();
  waitCondition_.notify_all();
  lock.unlock();
  if (worker_.joinable()) worker_.join();
  log::info("Shared inference service stopped");
}

InferenceServiceMetrics InferenceService::metrics() const noexcept {
  const auto now = std::chrono::steady_clock::now();
  const std::scoped_lock lock(metricsMutex_);
  InferenceServiceMetrics result;
  result.framesInferred = framesInferred_;
  result.inferenceErrors = inferenceErrors_;
  result.resultsRejected = resultsRejected_;
  result.modelReady = detector_ && detector_->isReady();
  result.running = running_.load(std::memory_order_acquire);
  const auto uptimeSeconds = std::chrono::duration<double>(now - startedAt_).count();
  if (uptimeSeconds > 0.0) {
    result.inferenceFps = static_cast<double>(framesInferred_) / uptimeSeconds;
    result.utilization = std::clamp(std::chrono::duration<double>(busyTime_).count() / uptimeSeconds, 0.0, 1.0);
  }
  if (!latencySamplesMs_.empty()) {
    double total = 0.0;
    for (const auto value : latencySamplesMs_) total += value;
    result.averageLatencyMs = total / static_cast<double>(latencySamplesMs_.size());
    result.p95LatencyMs = p95(latencySamplesMs_);
  }
  return result;
}

void InferenceService::run() noexcept {
  auto nextAllowed = std::chrono::steady_clock::now();
  while (running_.load(std::memory_order_acquire)) {
    if (config_.maximumTotalFps > 0.0) {
      std::unique_lock lock(waitMutex_);
      waitCondition_.wait_until(lock, nextAllowed,
                                [this] { return !running_.load(std::memory_order_acquire); });
      if (!running_.load(std::memory_order_acquire)) break;
    }
    auto scheduled = scheduler_->next();
    if (!scheduled || !running_.load(std::memory_order_acquire)) break;
    const auto begin = std::chrono::steady_clock::now();
    auto result = detector_->detect(scheduled->frame);
    const auto end = std::chrono::steady_clock::now();
    result.frameId = scheduled->frame.frameId;
    result.sourceId = scheduled->session->id();
    if (result.totalLatency.count() <= 0) {
      result.totalLatency = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
    }
    const bool inferenceOk = result.ok();
    const bool currentGeneration = scheduled->session->recordInferenceCompleted(result, scheduled->generation);
    bool accepted = false;
    if (currentGeneration) {
      accepted = router_.enqueue(scheduled->session, std::move(scheduled->frame), std::move(result),
                                 scheduled->generation);
    }
    {
      const std::scoped_lock lock(metricsMutex_);
      ++framesInferred_;
      if (!inferenceOk) ++inferenceErrors_;
      if (!accepted) ++resultsRejected_;
      busyTime_ += std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
      const auto latency = std::chrono::duration<double, std::milli>(end - begin).count();
      latencySamplesMs_.push_back(latency);
      if (latencySamplesMs_.size() > kLatencyWindow) latencySamplesMs_.pop_front();
    }
    if (config_.maximumTotalFps > 0.0) {
      nextAllowed = end + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(1.0 / config_.maximumTotalFps));
    }
  }
}

}  // namespace omnidetect
