#ifndef PHOX_RENDERER_HPP
#define PHOX_RENDERER_HPP

#include "phox/scene/Scene.hpp"

#include <memory>

namespace phox {

// Legacy renderer API: the scene types moved to phox::scene. Removed together with this renderer.
using namespace scene;

enum class RendererType {
    Vulkan,
};

class Renderer {
public:
    /// @brief Creates a renderer of a specified type/backend
    /// @param type the renderer backend to use
    /// @returns the new renderer
    /// @throws exception when a renderer of @c type couldn't be created
    static std::unique_ptr<Renderer> Create(const RendererType type);

    virtual ~Renderer() = default;
    virtual void initialize(bool debug = false) = 0;
    virtual void cleanup() = 0;
    virtual void render(const Scene &scene) = 0;
};

}

#endif // PHOX_RENDERER_HPP
