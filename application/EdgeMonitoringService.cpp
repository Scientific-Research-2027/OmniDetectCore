#include "application/EdgeMonitoringService.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace omnidetect {

EdgeMonitoringService::EdgeMonitoringService(AppConfig config,
                                             std::unique_ptr<IObjectDetector> detector,
                                             CameraManager::SourceFactory sourceFactory,
                                             CameraManager::TrackerFactory trackerFactory,
                                             CameraManager::ControlFactory controlFactory)
    : config_(std::move(config)) {
  try {
    if (config_.cameras.empty()) {
      initializationError_ = "Edge monitoring service requires at least one camera";
      return;
    }
    if (!detector) {
      initializationError_ = "Edge monitoring service requires one shared detector";
      return;
    }
    scheduler_ = std::make_shared<RoundRobinInferenceScheduler>(config_.sharedInference.maxFrameAge);
    const auto routerCapacity = std::max<std::size_t>(4U, config_.cameras.size() * 2U);
    router_ = std::make_unique<ResultRouter>(routerCapacity, events_);
    cameras_ = std::make_unique<CameraManager>(scheduler_, events_, std::move(sourceFactory),
                                               std::move(trackerFactory), std::move(controlFactory));
    if (!cameras_->configure(config_.cameras, initializationError_)) return;
    inference_ = std::make_unique<InferenceService>(std::move(detector), scheduler_, *router_,
                                                    config_.sharedInference);
  } catch (const std::exception& exception) {
    initializationError_ = std::string("Cannot initialize edge monitoring service: ") + exception.what();
  } catch (...) {
    initializationError_ = "Cannot initialize edge monitoring service";
  }
}

EdgeMonitoringService::~EdgeMonitoringService() { stop(); }

bool EdgeMonitoringService::addSink(std::shared_ptr<IResultSink> sink, std::string& error) noexcept {
  if (!router_) {
    error = initializationError_.empty() ? "Result router is not initialized" : initializationError_;
    return false;
  }
  return router_->addSink(std::move(sink), error);
}

bool EdgeMonitoringService::start(std::string& error) noexcept {
  if (!isReady() || !router_ || !inference_ || !cameras_) {
    error = initializationError_.empty() ? "Edge monitoring service is not ready" : initializationError_;
    return false;
  }
  if (running_.load(std::memory_order_acquire)) {
    error.clear();
    return true;
  }
  if (!router_->start(error)) return false;
  if (!inference_->start(error)) {
    router_->stop();
    return false;
  }
  if (!cameras_->startAll(config_.failFast, error)) {
    inference_->stop();
    router_->stop();
    return false;
  }
  startedAt_ = std::chrono::steady_clock::now();
  running_.store(true, std::memory_order_release);
  error.clear();
  return true;
}

void EdgeMonitoringService::stop() noexcept {
  if (!running_.exchange(false, std::memory_order_acq_rel)) return;
  if (cameras_) cameras_->stopAll();
  if (inference_) inference_->stop();
  if (router_) router_->stop();
}

bool EdgeMonitoringService::addCamera(const CameraConfig& config, std::string& error) noexcept {
  if (!cameras_ || !cameras_->addCamera(config, error)) return false;
  if (!isRunning()) return true;
  const auto session = cameras_->find(config.id);
  if (session && session->start(error)) return true;
  std::string ignored;
  cameras_->removeCamera(config.id, ignored);
  return false;
}

bool EdgeMonitoringService::removeCamera(const std::string& cameraId, std::string& error) noexcept {
  return cameras_ && cameras_->removeCamera(cameraId, error);
}

bool EdgeMonitoringService::restartCamera(const std::string& cameraId, std::string& error) noexcept {
  return cameras_ && cameras_->restartCamera(cameraId, error);
}

bool EdgeMonitoringService::stopCamera(const std::string& cameraId, std::string& error) noexcept {
  return cameras_ && cameras_->stopCamera(cameraId, error);
}

bool EdgeMonitoringService::sendManualControl(const std::string& cameraId,
                                              CameraControlCommand command,
                                              std::string& error) noexcept {
  return cameras_ && cameras_->sendManualControl(cameraId, command, error);
}

EdgeHealthSnapshot EdgeMonitoringService::metrics() const noexcept {
  EdgeHealthSnapshot result;
  result.inference = inference_ ? inference_->metrics() : InferenceServiceMetrics{};
  result.scheduler = scheduler_ ? scheduler_->metrics() : SchedulerMetrics{};
  result.router = router_ ? router_->metrics() : ResultRouterMetrics{};
  result.cameras = cameras_ ? cameras_->metrics() : std::vector<CameraHealthSnapshot>{};
  result.camerasConfigured = result.cameras.size();
  for (const auto& camera : result.cameras) {
    if (camera.state == CameraSessionState::Online) ++result.camerasOnline;
    else if (camera.state == CameraSessionState::Degraded || camera.state == CameraSessionState::Reconnecting) {
      ++result.camerasDegraded;
    } else {
      ++result.camerasOffline;
    }
  }
  if (isRunning()) {
    result.uptimeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
  }
  return result;
}

}  // namespace omnidetect
