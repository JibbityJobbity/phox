#ifndef PHOX_PLATFORM_LINUX_HPP
#define PHOX_PLATFORM_LINUX_HPP

#include <X11/Xlib.h>

#include "Platform.hpp"

namespace phox {

struct WindowHandle {
    Window window;
    Display *display;
};

static inline PlatformType GetPlatformType() { return PlatformType::Linux; }

} // namespace phox

#endif