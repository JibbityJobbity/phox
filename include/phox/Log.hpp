#ifndef PHOX_LOG_HPP
#define PHOX_LOG_HPP

#include <fmt/format.h>

#include <string_view>
#include <utility>

/// Minimal levelled logging to stderr. The level starts at Info, or at PHOX_LOG
/// (debug|info|warn|error|off) when that is set; Renderer::initialize lowers it to Debug in debug mode.
namespace phox::log {

enum class Level { Debug = 0, Info, Warn, Error, Off };

void setLevel(Level level);
Level level();
void write(Level level, std::string_view message);

template <class... Args> void debug(fmt::format_string<Args...> format, Args &&...args) {
    if (level() <= Level::Debug) write(Level::Debug, fmt::format(format, std::forward<Args>(args)...));
}
template <class... Args> void info(fmt::format_string<Args...> format, Args &&...args) {
    if (level() <= Level::Info) write(Level::Info, fmt::format(format, std::forward<Args>(args)...));
}
template <class... Args> void warn(fmt::format_string<Args...> format, Args &&...args) {
    if (level() <= Level::Warn) write(Level::Warn, fmt::format(format, std::forward<Args>(args)...));
}
template <class... Args> void error(fmt::format_string<Args...> format, Args &&...args) {
    if (level() <= Level::Error) write(Level::Error, fmt::format(format, std::forward<Args>(args)...));
}

} // namespace phox::log

#endif // PHOX_LOG_HPP
