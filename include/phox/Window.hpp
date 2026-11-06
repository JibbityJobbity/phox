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

/// The API a window presents with. It is fixed when the window is created.
enum class GraphicsApi {
    Vulkan,
    Metal, ///< Apple platforms only
};

/// The mouse, as of the last pollEvents().
struct PointerState {
    bool inside = false; ///< over the window
    float x = 0.0f;      ///< 0 at the left edge, 1 at the right
    float y = 0.0f;      ///< 0 at the top, 1 at the bottom
    bool pressed = false; ///< the primary button is held
};

struct WindowConfig {
    int x, y, width, height;
    WindowBackend backend;
    std::string title = "Phox";
    GraphicsApi api = GraphicsApi::Vulkan;
    WindowConfig();
};

class Window {
public:
    static std::unique_ptr<Window> Create(const WindowConfig& config = WindowConfig());
    virtual ~Window() = default;

    /// Pumps pending OS events. Returns false once the user has asked to close the window.
    virtual bool pollEvents() = 0;

    /// Where the mouse is and whether its button is held. Windows that do not track it report none.
    virtual PointerState pointer() const { return {}; }

    /// Size of the drawable area in pixels (may differ from the window size on HiDPI displays).
    virtual void framebufferSize(int &width, int &height) const = 0;

    /// Instance extensions Vulkan needs to create a surface for this window.
    virtual std::vector<std::string> requiredVulkanExtensions() const = 0;

    /// Creates a VkSurfaceKHR for this window. @p vkInstance is a VkInstance; the surface handle is
    /// returned through @p outSurface as raw bits so this header stays free of Vulkan types.
    /// The surface must be destroyed before the window.
    virtual bool createVulkanSurface(void *vkInstance, std::uint64_t *outSurface) = 0;

    /// The CAMetalLayer (as void *) that presents into this window, or nullptr if the window was not
    /// created for GraphicsApi::Metal. Owned by the window.
    virtual void *metalLayer() const = 0;
};

}

#endif // PHOX_WINDOW_HPP
