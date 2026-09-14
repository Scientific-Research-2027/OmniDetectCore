#pragma once

#include "core/detection/DetectionResult.h"
#include "core/monitoring/Event.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace omnidetect {

enum class MonitorState { None, Candidate, Confirmed, Lost };

struct MonitorConfig {
  std::uint32_t confirmationFrames{3};
  std::uint32_t lostFrames{10};
  float minimumConfidence{0.50F};
  std::unordered_set<int> targetClassIds;
  std::unordered_set<std::string> targetClassNames;
};

class ObjectMonitor {
 public:
  explicit ObjectMonitor(MonitorConfig config = {});
  virtual ~ObjectMonitor() = default;

  [[nodiscard]] std::vector<Event> update(const DetectionResult& result);
  void reset() noexcept;
  [[nodiscard]] MonitorState state() const noexcept { return state_; }

 protected:
  [[nodiscard]] virtual bool accepts(const Detection& detection) const;

 private:
  MonitorConfig config_;
  MonitorState state_{MonitorState::None};
  std::uint32_t observedFrames_{0};
  std::uint32_t missedFrames_{0};
  std::optional<Detection> lastDetection_;
};

}  // namespace omnidetect

