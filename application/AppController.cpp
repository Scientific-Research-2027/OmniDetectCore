#include "application/AppController.h"

namespace omnidetect {

AppController::~AppController() { stop(); }

bool AppController::setPipeline(std::unique_ptr<DetectionPipeline> pipeline, std::string& error) noexcept {
  if (!pipeline) {
    error = "Cannot configure a null pipeline";
    return false;
  }
  std::unique_ptr<DetectionPipeline> old;
  {
    const std::scoped_lock lock(mutex_);
    old = std::move(pipeline_);
    pipeline_ = std::move(pipeline);
  }
  if (old) old->stop();
  return true;
}

bool AppController::start(std::string& error) noexcept {
  const std::scoped_lock lock(mutex_);
  if (!pipeline_) {
    error = "No pipeline is configured";
    return false;
  }
  return pipeline_->start(error);
}

void AppController::stop() noexcept {
  const std::scoped_lock lock(mutex_);
  if (pipeline_) pipeline_->stop();
}

bool AppController::refresh(std::string& error) noexcept {
  const std::scoped_lock lock(mutex_);
  if (!pipeline_) {
    error = "No pipeline is configured";
    return false;
  }
  return pipeline_->restart(error);
}

bool AppController::isRunning() const noexcept {
  const std::scoped_lock lock(mutex_);
  return pipeline_ && pipeline_->isRunning();
}

PipelineMetrics AppController::metrics() const noexcept {
  const std::scoped_lock lock(mutex_);
  return pipeline_ ? pipeline_->metrics() : PipelineMetrics{};
}

DetectionPipeline* AppController::pipeline() noexcept {
  const std::scoped_lock lock(mutex_);
  return pipeline_.get();
}

}  // namespace omnidetect
