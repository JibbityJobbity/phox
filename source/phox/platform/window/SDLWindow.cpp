#include "phox/platform/SDLWindow.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <SDL2/SDL_vulkan.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace phox {

SDLWindow::SDLWindow(const WindowConfig &config) : m_api(config.api) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    const Uint32 apiFlag = m_api == GraphicsApi::Metal ? SDL_WINDOW_METAL : SDL_WINDOW_VULKAN;
    m_window = SDL_CreateWindow(config.title.c_str(), config.x, config.y, config.width, config.height,
                                apiFlag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (m_window == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
    }
    if (m_api == GraphicsApi::Metal) {
        m_metalView = SDL_Metal_CreateView(m_window); // a view backed by a CAMetalLayer
        if (m_metalView == nullptr) {
            const std::string error = SDL_GetError();
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            throw std::runtime_error("SDL_Metal_CreateView failed: " + error);
        }
    }
}

SDLWindow::~SDLWindow() {
    if (m_metalView != nullptr) {
        SDL_Metal_DestroyView(m_metalView);
    }
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
    if (m_api == GraphicsApi::Metal) {
        SDL_Metal_GetDrawableSize(m_window, &width, &height);
    } else {
        SDL_Vulkan_GetDrawableSize(m_window, &width, &height);
    }
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

void *SDLWindow::metalLayer() const {
    return m_metalView != nullptr ? SDL_Metal_GetLayer(m_metalView) : nullptr;
}

} // namespace phox
