#include "application/CameraManager.h"

#include "input/SourceFactory.h"
#include "control/CameraControlFactory.h"

#include <exception>
#include <utility>

namespace omnidetect {

CameraManager::CameraManager(std::shared_ptr<IInferenceScheduler> scheduler,
                             EventEngine& events,
                             SourceFactory sourceFactory,
                             TrackerFactory trackerFactory,
                             ControlFactory controlFactory)
    : scheduler_(std::move(scheduler)),
      events_(events),
      sourceFactory_(std::move(sourceFactory)),
      trackerFactory_(std::move(trackerFactory)),
      controlFactory_(std::move(controlFactory)) {
  if (!sourceFactory_) sourceFactory_ = createFrameSource;
  if (!trackerFactory_) {
    trackerFactory_ = [](const TrackerConfig& config) { return std::make_unique<IouObjectTracker>(config); };
  }
  if (!controlFactory_) controlFactory_ = createCameraControl;
}

CameraManager::~CameraManager() { stopAll(); }

bool CameraManager::configure(const std::vector<CameraConfig>& cameras, std::string& error) {
  if (size() != 0) {
    error = "CameraManager can only be configured once; add/remove cameras explicitly afterward";
    return false;
  }
  for (const auto& camera : cameras) {
    if (!camera.enabled) continue;
    if (!addCamera(camera, error)) {
      stopAll();
      std::vector<std::string> cameraIds;
      {
        const std::scoped_lock lock(mutex_);
        cameraIds.reserve(sessions_.size());
        for (const auto& [cameraId, unused] : sessions_) {
          static_cast<void>(unused);
          cameraIds.push_back(cameraId);
        }
      }
      for (const auto& cameraId : cameraIds) {
        std::string ignored;
        removeCamera(cameraId, ignored);
      }
      return false;
    }
  }
  if (size() == 0) {
    error = "No enabled cameras were configured";
    return false;
  }
  error.clear();
  return true;
}

bool CameraManager::addCamera(const CameraConfig& config, std::string& error) {
  if (config.id.empty()) {
    error = "Camera id must not be empty";
    return false;
  }
  {
    const std::scoped_lock lock(mutex_);
    if (sessions_.contains(config.id)) {
      error = "Duplicate camera id: " + config.id;
      return false;
    }
  }
  try {
    auto source = sourceFactory_(config.source, error);
    if (!source) {
      if (error.empty()) error = "Source factory returned null for camera " + config.id;
      return false;
    }
    auto tracker = trackerFactory_(config.tracker);
    if (!tracker) {
      error = "Tracker factory returned null for camera " + config.id;
      return false;
    }
    auto control = controlFactory_(config, error);
    if (config.control.enabled && !control) {
      if (error.empty()) error = "Control factory returned null for camera " + config.id;
      return false;
    }
    auto session = std::make_shared<CameraSession>(config, std::move(source), std::move(tracker), events_,
                                                   std::move(control));
    std::weak_ptr<IInferenceScheduler> weakScheduler = scheduler_;
    session->setFrameNotifier([weakScheduler] {
      if (const auto scheduler = weakScheduler.lock()) scheduler->notifyFrameAvailable();
    });
    if (!scheduler_->addCamera(session, error)) return false;
    {
      const std::scoped_lock lock(mutex_);
      sessions_.emplace(config.id, std::move(session));
    }
    return true;
  } catch (const std::exception& exception) {
    error = std::string("Cannot add camera ") + config.id + ": " + exception.what();
    return false;
  }
}

bool CameraManager::removeCamera(const std::string& cameraId, std::string& error) noexcept {
  std::shared_ptr<CameraSession> session;
  {
    const std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(cameraId);
    if (found == sessions_.end()) {
      error = "Camera not found: " + cameraId;
      return false;
    }
    session = std::move(found->second);
    sessions_.erase(found);
  }
  session->stop();
  scheduler_->removeCamera(cameraId);
  error.clear();
  return true;
}

bool CameraManager::startAll(const bool failFast, std::string& error) noexcept {
  std::vector<std::shared_ptr<CameraSession>> sessions;
  {
    const std::scoped_lock lock(mutex_);
    for (const auto& [unused, session] : sessions_) {
      static_cast<void>(unused);
      sessions.push_back(session);
    }
  }
  bool anyStarted = false;
  for (const auto& session : sessions) {
    std::string cameraError;
    if (session->start(cameraError)) {
      anyStarted = true;
    } else if (failFast) {
      error = cameraError;
      stopAll();
      return false;
    } else {
      events_.publish({EventType::CameraDisconnected, std::chrono::system_clock::now(), 0, session->id(),
                       std::nullopt, cameraError, {}});
    }
  }
  if (!anyStarted) {
    error = "No camera capture worker could be started";
    return false;
  }
  if (failFast) {
    for (const auto& session : sessions) {
      std::string connectionError;
      if (!session->waitForInitialConnection(session->config().startupTimeout, connectionError)) {
        error = connectionError;
        stopAll();
        return false;
      }
    }
  }
  error.clear();
  return true;
}

void CameraManager::stopAll() noexcept {
  std::vector<std::shared_ptr<CameraSession>> sessions;
  {
    const std::scoped_lock lock(mutex_);
    for (const auto& [unused, session] : sessions_) {
      static_cast<void>(unused);
      sessions.push_back(session);
    }
  }
  for (const auto& session : sessions) session->stop();
}

bool CameraManager::restartCamera(const std::string& cameraId, std::string& error) noexcept {
  const auto session = find(cameraId);
  if (!session) {
    error = "Camera not found: " + cameraId;
    return false;
  }
  return session->restart(error);
}

bool CameraManager::stopCamera(const std::string& cameraId, std::string& error) noexcept {
  const auto session = find(cameraId);
  if (!session) {
    error = "Camera not found: " + cameraId;
    return false;
  }
  session->stop();
  error.clear();
  return true;
}

bool CameraManager::sendManualControl(const std::string& cameraId, CameraControlCommand command,
                                      std::string& error) noexcept {
  const auto session = find(cameraId);
  if (!session) {
    error = "Camera not found: " + cameraId;
    return false;
  }
  if (!session->sendManualControl(command)) {
    error = "Camera control is unavailable or not running for " + cameraId;
    return false;
  }
  error.clear();
  return true;
}

std::shared_ptr<CameraSession> CameraManager::find(const std::string& cameraId) const noexcept {
  const std::scoped_lock lock(mutex_);
  const auto found = sessions_.find(cameraId);
  return found == sessions_.end() ? nullptr : found->second;
}

std::vector<CameraHealthSnapshot> CameraManager::metrics() const noexcept {
  std::vector<std::shared_ptr<CameraSession>> sessions;
  {
    const std::scoped_lock lock(mutex_);
    for (const auto& [unused, session] : sessions_) {
      static_cast<void>(unused);
      sessions.push_back(session);
    }
  }
  std::vector<CameraHealthSnapshot> result;
  result.reserve(sessions.size());
  for (const auto& session : sessions) result.push_back(session->metrics());
  return result;
}

std::size_t CameraManager::size() const noexcept {
  const std::scoped_lock lock(mutex_);
  return sessions_.size();
}

}  // namespace omnidetect
