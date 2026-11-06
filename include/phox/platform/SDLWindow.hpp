#ifndef PHOX_SDLWINDOW_HPP
#define PHOX_SDLWINDOW_HPP

#include "phox/Window.hpp"

struct SDL_Window;

namespace phox {

class SDLWindow : public Window {
public:
    explicit SDLWindow(const WindowConfig &config);
    ~SDLWindow() override;
    SDLWindow(const SDLWindow &) = delete;
    SDLWindow &operator=(const SDLWindow &) = delete;

    bool pollEvents() override;
    PointerState pointer() const override { return m_pointer; }
    void framebufferSize(int &width, int &height) const override;
    std::vector<std::string> requiredVulkanExtensions() const override;
    bool createVulkanSurface(void *vkInstance, std::uint64_t *outSurface) override;
    void *metalLayer() const override;

private:
    SDL_Window *m_window = nullptr;
    GraphicsApi m_api = GraphicsApi::Vulkan;
    void *m_metalView = nullptr; ///< SDL_MetalView, for GraphicsApi::Metal
    bool m_closeRequested = false;
    PointerState m_pointer;
};

}

#endif // PHOX_SDLWINDOW_HPP
