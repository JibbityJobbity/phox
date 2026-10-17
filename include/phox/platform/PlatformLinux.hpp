#ifndef PHOX_PLATFORM_LINUX_HPP
#define PHOX_PLATFORM_LINUX_HPP

#include "Platform.hpp"

namespace phox {

// Opaque X11 handles, so Xlib (and its macros/`Window` typedef) stays out of public headers.
struct WindowHandle {
    unsigned long window = 0; // ::Window
    void *display = nullptr;  // ::Display*
};

static inline PlatformType GetPlatformType() { return PlatformType::Linux; }

} // namespace phox

#endif
