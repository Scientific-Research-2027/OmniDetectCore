#include "core/logging/Logger.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>

#ifdef OMNIDETECT_HAS_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace omnidetect::log {
namespace {
std::atomic<Level> currentLevel{Level::Info};
std::mutex fallbackMutex;

const char* label(const Level level) noexcept {
  switch (level) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warning: return "warning";
    case Level::Error: return "error";
    case Level::Critical: return "critical";
  }
  return "unknown";
}
}  // namespace

void setLevel(const Level level) noexcept {
  currentLevel.store(level, std::memory_order_relaxed);
#ifdef OMNIDETECT_HAS_SPDLOG
  spdlog::set_level(static_cast<spdlog::level::level_enum>(static_cast<int>(level)));
#endif
}

void write(const Level level, const std::string_view message) noexcept {
  if (static_cast<int>(level) < static_cast<int>(currentLevel.load(std::memory_order_relaxed))) return;
#ifdef OMNIDETECT_HAS_SPDLOG
  spdlog::log(static_cast<spdlog::level::level_enum>(static_cast<int>(level)), "{}", message);
#else
  try {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    const std::scoped_lock lock(fallbackMutex);
    std::clog << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " [" << label(level) << "] " << message << '\n';
  } catch (...) {
    // Logging must never terminate a realtime worker.
  }
#endif
}

}  // namespace omnidetect::log

