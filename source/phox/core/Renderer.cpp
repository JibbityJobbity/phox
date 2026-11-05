#include "phox/Renderer.hpp"

#include "phox/vulkan/Renderer.hpp"
#ifdef PHOX_HAS_METAL
#include "phox/metal/Renderer.hpp"
#endif

#include <stdexcept>

namespace phox {

std::unique_ptr<Renderer> Renderer::Create(const RendererType type) {
    switch (type) {
        case RendererType::Vulkan:
            return std::make_unique<vk::Renderer>();
        case RendererType::Metal:
#ifdef PHOX_HAS_METAL
            return std::make_unique<mtl::Renderer>();
#else
            throw std::runtime_error("this build of Phox has no Metal backend (Apple platforms only)");
#endif
        default:
            throw std::runtime_error("Unsupported backend");
    };
}

bool Renderer::Available(const RendererType type) {
    switch (type) {
        case RendererType::Vulkan:
            return true;
        case RendererType::Metal:
#ifdef PHOX_HAS_METAL
            return true;
#else
            return false;
#endif
    }
    return false;
}

}
