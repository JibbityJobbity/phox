#ifndef PHOX_WINDOW_HPP
#define PHOX_WINDOW_HPP

#include <memory>

namespace phox {

enum class WindowBackend {
    SDL,
    Default
};

struct WindowConfig {
    int x, y, width, height;
    WindowBackend backend;
    WindowConfig();
};

class Window {
public:
    static std::unique_ptr<Window> Create(const WindowConfig& config = WindowConfig());
    virtual ~Window() = default;
};

}

#endif // PHOX_WINDOW_HPP