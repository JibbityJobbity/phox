#include "phox/platform/SDLWindow.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace phox {

SDLWindow::SDLWindow(const WindowConfig &config) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    m_window = SDL_CreateWindow(config.title.c_str(), config.x, config.y, config.width, config.height,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (m_window == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
    }
}

SDLWindow::~SDLWindow() {
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

bool SDLWindow::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            m_closeRequested = true;
        }
    }
    return !m_closeRequested;
}

void SDLWindow::framebufferSize(int &width, int &height) const {
    SDL_Vulkan_GetDrawableSize(m_window, &width, &height);
}

std::vector<std::string> SDLWindow::requiredVulkanExtensions() const {
    unsigned int count = 0;
    if (SDL_Vulkan_GetInstanceExtensions(m_window, &count, nullptr) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    std::vector<const char *> names(count);
    if (SDL_Vulkan_GetInstanceExtensions(m_window, &count, names.data()) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    return std::vector<std::string>(names.begin(), names.begin() + count);
}

bool SDLWindow::createVulkanSurface(void *vkInstance, std::uint64_t *outSurface) {
    VkSurfaceKHR surface{}; // value-initialised null handle (SDL's Vulkan typedefs omit VK_NULL_HANDLE)
    if (SDL_Vulkan_CreateSurface(m_window, reinterpret_cast<VkInstance>(vkInstance), &surface) != SDL_TRUE) {
        return false;
    }
    static_assert(sizeof(surface) <= sizeof(std::uint64_t), "VkSurfaceKHR must fit in 64 bits");
    std::uint64_t raw = 0;
    std::memcpy(&raw, &surface, sizeof(surface));
    *outSurface = raw;
    return true;
}

} // namespace phox
