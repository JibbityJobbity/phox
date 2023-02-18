#ifndef PHOX_PLATFORM_WINDOW_HPP
#define PHOX_PLATFORM_WINDOW_HPP

namespace phox {

struct PlatformWindow {
#ifdef _WIN32
    void *handle;
};
} // namespace phox

#endif // PHOX_PLATFORM_WINDOW_HPP
