#include "phox/Renderer.hpp"

#include "phox/vulkan/Renderer.hpp"

#include <stdexcept>

namespace phox {

std::unique_ptr<Renderer> Renderer::Create(const RendererType type) {
    switch (type) {
        case RendererType::Vulkan:
            return std::make_unique<vk::Renderer>();
        default:
            throw std::runtime_error("Unsupported backend");
    };
}

}
