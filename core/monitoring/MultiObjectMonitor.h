#pragma once

#include "core/detection/DetectionResult.h"
#include "core/monitoring/Event.h"
#include "core/monitoring/ObjectMonitor.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnidetect {

class IMonitorPolicy {
 public:
  virtual ~IMonitorPolicy() = default;
  [[nodiscard]] virtual bool accepts(const Detection& detection) const noexcept = 0;
};

class ClassFilterMonitorPolicy final : public IMonitorPolicy {
 public:
  explicit ClassFilterMonitorPolicy(MonitorConfig config);
  [[nodiscard]] bool accepts(const Detection& detection) const noexcept override;

 private:
  MonitorConfig config_;
};

struct MonitoredObjectState {
  std::string cameraId;
  std::uint64_t trackId{0};
  int classId{-1};
  std::string className;
  MonitorState state{MonitorState::Candidate};
  std::uint32_t observedFrames{0};
  std::uint32_t missedFrames{0};
  std::uint64_t lastFrameId{0};
  Detection lastDetection;
};

class MultiObjectMonitor {
 public:
  explicit MultiObjectMonitor(MonitorConfig config = {}, std::shared_ptr<IMonitorPolicy> policy = {});

  [[nodiscard]] std::vector<Event> update(const DetectionResult& result);
  void reset() noexcept;
  [[nodiscard]] std::vector<MonitoredObjectState> snapshot() const;

 private:
  struct ObjectKey {
    std::string cameraId;
    std::uint64_t trackId{0};
    bool operator==(const ObjectKey&) const = default;
  };

  struct ObjectKeyHash {
    [[nodiscard]] std::size_t operator()(const ObjectKey& key) const noexcept;
  };

  MonitorConfig config_;
  std::shared_ptr<IMonitorPolicy> policy_;
  mutable std::mutex mutex_;
  std::unordered_map<ObjectKey, MonitoredObjectState, ObjectKeyHash> states_;
};

}  // namespace omnidetect

