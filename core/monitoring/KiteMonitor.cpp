#include "core/monitoring/KiteMonitor.h"

#include <utility>

namespace omnidetect {
namespace {
MonitorConfig kiteDefaults(MonitorConfig config) {
  if (config.targetClassIds.empty() && config.targetClassNames.empty()) {
    config.targetClassNames.insert("kite");
  }
  return config;
}
}  // namespace

KiteMonitor::KiteMonitor(MonitorConfig config) : ObjectMonitor(kiteDefaults(std::move(config))) {}

}  // namespace omnidetect
