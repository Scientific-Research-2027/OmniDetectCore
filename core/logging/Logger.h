#pragma once

#include <string_view>

namespace omnidetect::log {

enum class Level { Trace, Debug, Info, Warning, Error, Critical };

void setLevel(Level level) noexcept;
void write(Level level, std::string_view message) noexcept;

inline void info(std::string_view message) noexcept { write(Level::Info, message); }
inline void warning(std::string_view message) noexcept { write(Level::Warning, message); }
inline void error(std::string_view message) noexcept { write(Level::Error, message); }
inline void debug(std::string_view message) noexcept { write(Level::Debug, message); }

}  // namespace omnidetect::log

