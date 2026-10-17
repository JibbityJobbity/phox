#ifndef PHOX_PLATFORM_MACOS_HPP
#define PHOX_PLATFORM_MACOS_HPP

#include "Platform.hpp"

namespace phox {

struct WindowHandle {};

static inline PlatformType GetPlatformType() { return PlatformType::MacOS; }

} // namespace phox

#endif