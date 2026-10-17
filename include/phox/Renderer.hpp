#ifndef PHOX_RENDERER_HPP
#define PHOX_RENDERER_HPP

#include "phox/Scene.hpp"

#include <memory>

namespace phox {

class Window;

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
    /// @param debug   enable validation layers and verbose logging
    /// @param window  optional; when given the renderer presents to it. The window must outlive
    ///                the renderer (call cleanup() before destroying it). Without one, rendering is
    ///                headless and the frame is written to build/rendered_triangle.ppm.
    virtual void initialize(bool debug = false, Window *window = nullptr) = 0;
    virtual void cleanup() = 0;
    virtual void render(const Scene &scene) = 0;
};

}

#endif // PHOX_RENDERER_HPP
