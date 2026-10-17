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
    void framebufferSize(int &width, int &height) const override;
    std::vector<std::string> requiredVulkanExtensions() const override;
    bool createVulkanSurface(void *vkInstance, std::uint64_t *outSurface) override;

private:
    SDL_Window *m_window = nullptr;
    bool m_closeRequested = false;
};

}

#endif // PHOX_SDLWINDOW_HPP
