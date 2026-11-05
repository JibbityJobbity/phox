#ifndef PHOX_RHI_LOG_HPP
#define PHOX_RHI_LOG_HPP

#include <string_view>

namespace phox {

enum class LogLevel { Debug, Info, Warning, Error };
using LogFn = void (*)(LogLevel, std::string_view);

/// Log output of the RHI stack (PhoxCore and its backends), which depends on nothing but the standard
/// library. The legacy renderer logs through phox::log (phox/Log.hpp) instead.
///
/// Replaces the sink for library log output. Passing nullptr restores the default,
/// which writes Warning and Error messages to stderr.
void setLogCallback(LogFn);
void logMessage(LogLevel, std::string_view);

} // namespace phox

#endif // PHOX_RHI_LOG_HPP
