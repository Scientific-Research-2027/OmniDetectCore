#pragma once

#include "core/monitoring/ObjectMonitor.h"

namespace omnidetect {

class KiteMonitor final : public ObjectMonitor {
 public:
  explicit KiteMonitor(MonitorConfig config = {});
};

}  // namespace omnidetect

