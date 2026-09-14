#include "application/DetectionPipeline.h"

#include "core/logging/Logger.h"

#include <chrono>
#include <exception>
#include <utility>

namespace omnidetect {

DetectionPipeline::DetectionPipeline(std::unique_ptr<IFrameSource> source,
                                     std::unique_ptr<IObjectDetector> detector,
                                     PipelineConfig pipelineConfig,
                                     TrackerConfig trackerConfig,
                                     MonitorConfig monitorConfig)
    : source_(std::move(source)),
      detector_(std::move(detector)),
      config_(pipelineConfig),
      tracker_(trackerConfig),
      monitor_(std::move(monitorConfig)),
      frameQueue_(pipelineConfig.frameQueueSize, pipelineConfig.dropOldFrames),
      resultQueue_(pipelineConfig.resultQueueSize, pipelineConfig.dropOldFrames) {}

DetectionPipeline::~DetectionPipeline() { stop(); }

bool DetectionPipeline::addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept {
  if (!sink) {
    error = "Cannot add a null result sink";
    return false;
  }
  if (isRunning()) {
    error = "Add sinks before starting the pipeline";
    return false;
  }
  const std::scoped_lock lock(sinksMutex_);
  sinks_.push_back(std::move(sink));
  error.clear();
  return true;
}

bool DetectionPipeline::start(std::string& error) noexcept {
  if (captureThread_.joinable() || inferenceThread_.joinable() || deliveryThread_.joinable()) {
    stop();
  }
  const std::scoped_lock lock(lifecycleMutex_);
  if (!source_) {
    error = "Pipeline has no frame source";
    return false;
  }
  if (!detector_ || !detector_->isReady()) {
    error = "Pipeline detector is not ready";
    return false;
  }
  if (!source_->open(error)) return false;
  {
    const std::scoped_lock sinksLock(sinksMutex_);
    for (const auto& sink : sinks_) {
      if (!sink->start(error)) {
        for (const auto& startedSink : sinks_) startedSink->stop();
        source_->close();
        error = "Failed to start result sink " + sink->name() + ": " + error;
        return false;
      }
    }
  }

  tracker_.reset();
  monitor_.reset();
  frameQueue_.reset();
  resultQueue_.reset();
  capturedFrames_.store(0);
  inferredFrames_.store(0);
  deliveredResults_.store(0);
  droppedFrames_.store(0);
  droppedResults_.store(0);
  totalLatencyMicroseconds_.store(0);
  startedAt_ = std::chrono::steady_clock::now();
  const auto sourceId = source_->metadata().id;
  running_.store(true, std::memory_order_release);
  try {
    deliveryThread_ = std::thread(&DetectionPipeline::deliveryLoop, this);
    inferenceThread_ = std::thread(&DetectionPipeline::inferenceLoop, this);
    captureThread_ = std::thread(&DetectionPipeline::captureLoop, this);
  } catch (const std::exception& exception) {
    error = std::string("Failed to create pipeline workers: ") + exception.what();
    running_.store(false, std::memory_order_release);
    frameQueue_.close();
    resultQueue_.close();
    if (captureThread_.joinable()) captureThread_.join();
    if (inferenceThread_.joinable()) inferenceThread_.join();
    if (deliveryThread_.joinable()) deliveryThread_.join();
    source_->close();
    {
      const std::scoped_lock sinksLock(sinksMutex_);
      for (const auto& sink : sinks_) sink->stop();
    }
    return false;
  }
  log::info("Detection pipeline started for source " + sourceId);
  return true;
}

void DetectionPipeline::stop() noexcept {
  std::unique_lock lock(lifecycleMutex_);
  const bool wasActive = running_.load(std::memory_order_acquire) || captureThread_.joinable() ||
                         inferenceThread_.joinable() || deliveryThread_.joinable();
  running_.store(false, std::memory_order_release);
  frameQueue_.close();
  resultQueue_.close();
  lock.unlock();
  if (captureThread_.joinable()) captureThread_.join();
  if (inferenceThread_.joinable()) inferenceThread_.join();
  if (deliveryThread_.joinable()) deliveryThread_.join();
  lock.lock();
  if (source_) source_->close();
  if (wasActive) {
    const std::scoped_lock sinksLock(sinksMutex_);
    for (const auto& sink : sinks_) sink->stop();
    log::info("Detection pipeline stopped");
  }
}

bool DetectionPipeline::restart(std::string& error) noexcept { return start(error); }

PipelineMetrics DetectionPipeline::metrics() const noexcept {
  PipelineMetrics output;
  output.capturedFrames = capturedFrames_.load();
  output.inferredFrames = inferredFrames_.load();
  output.deliveredResults = deliveredResults_.load();
  output.droppedFrames = droppedFrames_.load();
  output.droppedResults = droppedResults_.load();
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
  if (seconds > 0.0) {
    output.captureFps = static_cast<double>(output.capturedFrames) / seconds;
    output.inferenceFps = static_cast<double>(output.inferredFrames) / seconds;
  }
  if (output.inferredFrames > 0) {
    output.averageLatencyMs = static_cast<double>(totalLatencyMicroseconds_.load()) /
                              static_cast<double>(output.inferredFrames) / 1000.0;
  }
  return output;
}

void DetectionPipeline::captureLoop() noexcept {
  bool disconnected = false;
  while (running_.load(std::memory_order_acquire)) {
    auto read = source_->read();
    if (read.status == FrameReadStatus::Ready) {
      if (disconnected) {
        publishSourceEvent(EventType::SourceRecovered, "Frame source recovered");
        disconnected = false;
      }
      ++capturedFrames_;
      const auto pushed = frameQueue_.push(std::move(read.frame));
      droppedFrames_.fetch_add(pushed.dropped);
      if (!pushed.accepted) break;
    } else if (read.status == FrameReadStatus::TemporaryError) {
      if (!disconnected) publishSourceEvent(EventType::SourceDisconnected, read.error);
      disconnected = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } else {
      if (read.status == FrameReadStatus::FatalError) {
        publishSourceEvent(EventType::SourceDisconnected, read.error);
        log::error("Frame source stopped: " + read.error);
      }
      break;
    }
  }
  frameQueue_.close();
}

void DetectionPipeline::inferenceLoop() noexcept {
  while (running_.load(std::memory_order_acquire) || !frameQueue_.closed()) {
    auto frame = frameQueue_.pop();
    if (!frame) break;
    auto result = detector_->detect(*frame);
    ++inferredFrames_;
    totalLatencyMicroseconds_.fetch_add(static_cast<std::uint64_t>(std::max<std::int64_t>(0, result.totalLatency.count())));
    if (!result.ok()) {
      eventEngine_.publish({EventType::ModelError, std::chrono::system_clock::now(), result.frameId,
                            result.sourceId, std::nullopt, result.error, {}});
    }
#ifdef OMNIDETECT_ENABLE_TRACKING
    if (config_.trackingEnabled && result.ok()) tracker_.update(result);
#endif
    if (config_.monitoringEnabled && result.ok()) {
      for (const auto& event : monitor_.update(result)) eventEngine_.publish(event);
    }
    const auto pushed = resultQueue_.push(ResultPacket{std::move(*frame), std::move(result)});
    droppedResults_.fetch_add(pushed.dropped);
    if (!pushed.accepted) break;
  }
  resultQueue_.close();
}

void DetectionPipeline::deliveryLoop() noexcept {
  while (running_.load(std::memory_order_acquire) || !resultQueue_.closed()) {
    auto packet = resultQueue_.pop();
    if (!packet) break;
    std::vector<std::shared_ptr<IResultSink>> sinks;
    {
      const std::scoped_lock lock(sinksMutex_);
      sinks = sinks_;
    }
    for (const auto& sink : sinks) {
      try {
        sink->consume(packet->frame, packet->result);
      } catch (const std::exception& exception) {
        log::error("Result sink " + sink->name() + " threw: " + exception.what());
      } catch (...) {
        log::error("Result sink " + sink->name() + " threw an unknown error");
      }
    }
    ++deliveredResults_;
  }
  running_.store(false, std::memory_order_release);
}

void DetectionPipeline::publishSourceEvent(const EventType type, const std::string& message) noexcept {
  const auto metadata = source_->metadata();
  eventEngine_.publish({type, std::chrono::system_clock::now(), 0, metadata.id, std::nullopt, message, {}});
}

}  // namespace omnidetect
