#ifndef PHOX_RENDERER_HPP
#define PHOX_RENDERER_HPP

#include "phox/CpuImage.hpp"
#include "phox/Resources.hpp"
#include "phox/Scene.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phox {

class Window;

enum class RendererType {
    Vulkan,
    Metal, ///< Apple platforms, when built with PHOX_ENABLE_METAL (the default there)
};

struct RendererConfig {
    /// Enables debug logging and validation: the Khronos validation layer with synchronization validation
    /// (Vulkan), or Metal API validation (MTL_DEBUG_LAYER).
    bool debug = false;
    /// When set, frames are presented to this window, which must outlive the renderer (call cleanup()
    /// before destroying it) and must have been created for the renderer's API (WindowConfig::api).
    /// When null, rendering is headless and each frame is read back (lastFrame()).
    Window *window = nullptr;
    /// Size of headless frames.
    Extent2D headlessExtent{640, 480};
    /// Case-insensitive substring of the GPU name to use. Empty means the PHOX_DEVICE environment
    /// variable if set, otherwise the best available device.
    std::string preferredDevice;
    /// Keep each frame's render graph as Graphviz DOT (frameGraphDot()). Costs a little per frame.
    bool captureFrameGraph = false;
    /// Draw with task and mesh shaders (VK_EXT_mesh_shader) where the device has them, as meshlets that
    /// the task shader culls. Otherwise, or when off, meshes are drawn with vertex shaders.
    bool meshShaders = true;
    /// With mesh shaders: cull meshlets outside the view frustum and, for single-sided materials, meshlets
    /// facing away from the camera. Off only to measure what culling gains.
    bool meshletCulling = true;
    /// With meshlet culling: also cull meshlets hidden behind others, drawing in two passes around a
    /// depth pyramid. Needs a D32Float depth buffer (nearly every device).
    bool occlusionCulling = true;
};

/// GPU time of one render graph pass.
struct PassTiming {
    std::string pass;
    double milliseconds = 0.0;
};

enum class RendererBackend { None, Vulkan, Metal, Software };

struct RendererInfo {
    RendererBackend backend = RendererBackend::None;
    std::string deviceName;
    bool validation = false;              ///< the validation layer is active
    /// Vulkan: error messages reported by the validation layer so far. Metal: command buffers that failed
    /// (Metal's validation layer stops the process on an error instead of reporting it).
    std::uint32_t validationErrors = 0;
    std::uint32_t validationWarnings = 0; ///< warning messages reported by the validation layer so far (Vulkan)
    bool meshShaders = false;             ///< meshes are drawn as meshlets with task and mesh shaders
    bool occlusionCulling = false;        ///< meshlets are also culled against a depth pyramid
};

class Renderer {
public:
    /// @brief Creates a renderer of a specified type/backend
    /// @throws exception when a renderer of @c type couldn't be created
    static std::unique_ptr<Renderer> Create(const RendererType type);
    /// Whether this build includes the backend for @p type.
    static bool Available(RendererType type);

    virtual ~Renderer() = default;
    /// Falls back to a software rasterizer (headless only) if the GPU backend cannot start.
    virtual void initialize(const RendererConfig &config) = 0;
    virtual void cleanup() = 0;
    virtual void render(const Scene &scene) = 0;
    /// The most recent headless frame. Empty before the first headless frame and when windowed.
    virtual const CpuImage &lastFrame() const = 0;
    virtual RendererInfo info() const = 0;
    /// The most recent frame's compiled render graph in Graphviz DOT. Empty unless
    /// RendererConfig::captureFrameGraph is set (and for the software fallback, which has no graph).
    virtual const std::string &frameGraphDot() const = 0;
    /// GPU time per pass of the most recent frame the GPU has finished, which when windowed is a frame or
    /// two behind the last render() call. Empty before then, for the software fallback, and on devices
    /// without timestamps.
    virtual const std::vector<PassTiming> &gpuTimings() const = 0;
};

}

#endif // PHOX_RENDERER_HPP
