#ifndef PHOX_LOG_HPP
#define PHOX_LOG_HPP

#include <string_view>

namespace phox {

enum class LogLevel { Debug, Info, Warning, Error };
using LogFn = void (*)(LogLevel, std::string_view);

/// Replaces the sink for library log output. Passing nullptr restores the default,
/// which writes Warning and Error messages to stderr.
void setLogCallback(LogFn);
void log(LogLevel, std::string_view);

} // namespace phox

#endif // PHOX_LOG_HPP
