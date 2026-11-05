#ifndef PHOX_VULKAN_RENDERER_HPP
#define PHOX_VULKAN_RENDERER_HPP

#include "phox/Renderer.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;
class MeshCache;
class TextureCache;
class MeshletVisibility;
class PipelineCache;
class ShaderLibrary;
class Swapchain;
class TransientPool;

/// Messages counted by the debug messenger (written from the validation layer's callback).
struct ValidationCounters {
    std::atomic<std::uint32_t> errors{0};
    std::atomic<std::uint32_t> warnings{0};
};

class Renderer final : public phox::Renderer {
public:
    /// Frames the CPU may record ahead of the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    Renderer();
    ~Renderer() override;

    void initialize(const RendererConfig &config) override;
    void cleanup() override;
    void render(const Scene &scene) override;
    const CpuImage &lastFrame() const override { return m_lastFrame; }
    RendererInfo info() const override;
    const std::string &frameGraphDot() const override { return m_frameGraphDot; }
    const std::vector<PassTiming> &gpuTimings() const override { return m_gpuTimings; }

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void createDevice();
    /// Creates the shader library and pipeline cache and builds the main pipelines. Returns whether the
    /// meshlet pipelines (task and mesh shaders) could be built as well.
    bool createPipelines();
    /// Destroys every Vulkan object, waiting for the GPU first. Safe on a partially initialised renderer.
    void releaseVulkan();

    /// Makes sure the swapchain matches the window. Returns false while the window has no area.
    bool ensureSwapchain();
    void recreateSwapchain();

    void renderFallback(const Scene &scene);

    RendererConfig m_config;
    Window *m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::unique_ptr<Device> m_device;

    std::unique_ptr<Swapchain> m_swapchain;
    Extent2D m_windowSize; ///< framebuffer size the swapchain was created for
    /// Signalled by a frame's submission and waited on by its present. One per swapchain image rather than
    /// per frame in flight: a frame slot can come round again before the presentation engine has consumed
    /// the previous wait. Only ever grows, so semaphores are never destroyed while a present uses them.
    std::vector<VkSemaphore> m_renderFinished;

    std::array<std::unique_ptr<FrameContext>, kFramesInFlight> m_frames;
    std::uint64_t m_frameNumber = 0;
    std::unique_ptr<TransientPool> m_transients;
    std::unique_ptr<MeshCache> m_meshes;
    std::unique_ptr<TextureCache> m_textures;
    std::unique_ptr<MeshletVisibility> m_visibility; ///< occlusion culling only
    std::unique_ptr<Buffer> m_readback; ///< headless only

    std::unique_ptr<ShaderLibrary> m_shaders;
    std::unique_ptr<PipelineCache> m_pipelines;
    Format m_depthFormat = Format::D32Float;

    CpuImage m_lastFrame;
    std::string m_frameGraphDot;
    std::vector<PassTiming> m_gpuTimings;
    std::string m_deviceName;
    ValidationCounters m_validationCounters;
    bool m_validation = false;
    bool m_initialized = false;
    bool m_useFallback = false;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_RENDERER_HPP
