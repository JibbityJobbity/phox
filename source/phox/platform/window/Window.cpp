#include "phox/Window.hpp"

#include "phox/platform/SDLWindow.hpp"

using namespace phox;

#include <stdexcept>

WindowConfig::WindowConfig() : x(200), y(200), width(1600), height(900), backend(WindowBackend::SDL) {}

std::unique_ptr<Window> Window::Create(const WindowConfig& config) {
    switch (config.backend) {
        case WindowBackend::Default:
        case WindowBackend::SDL:
            return std::make_unique<SDLWindow>(config);
        default:
            throw std::runtime_error("Unsupported window backend!");
    }
}
