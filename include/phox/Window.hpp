#ifndef PHOX_WINDOW_HPP
#define PHOX_WINDOW_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phox {

enum class WindowBackend {
    SDL,
    Default
};

struct WindowConfig {
    int x, y, width, height;
    WindowBackend backend;
    std::string title = "Phox";
    WindowConfig();
};

class Window {
public:
    static std::unique_ptr<Window> Create(const WindowConfig& config = WindowConfig());
    virtual ~Window() = default;

    /// Pumps pending OS events. Returns false once the user has asked to close the window.
    virtual bool pollEvents() = 0;

    /// Size of the drawable area in pixels (may differ from the window size on HiDPI displays).
    virtual void framebufferSize(int &width, int &height) const = 0;

    /// Instance extensions Vulkan needs to create a surface for this window.
    virtual std::vector<std::string> requiredVulkanExtensions() const = 0;

    /// Creates a VkSurfaceKHR for this window. @p vkInstance is a VkInstance; the surface handle is
    /// returned through @p outSurface as raw bits so this header stays free of Vulkan types.
    /// The surface must be destroyed before the window.
    virtual bool createVulkanSurface(void *vkInstance, std::uint64_t *outSurface) = 0;
};

}

#endif // PHOX_WINDOW_HPP
