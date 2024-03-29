#ifndef PHOX_PLATFORM_HPP
#define PHOX_PLATFORM_HPP

namespace phox {

enum PlatformType { Unknown, Windows, Linux, MacOS };

} // namespace phox

#ifdef __linux__
#include "PlatformLinux.hpp"
#endif

#ifdef __APPLE__
#include "PlatformMacOS.hpp"
#endif

#endif // PHOX_PLATFORM_HPP
