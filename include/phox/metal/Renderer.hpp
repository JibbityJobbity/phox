#ifndef PHOX_METAL_RENDERER_HPP
#define PHOX_METAL_RENDERER_HPP

#include "phox/Renderer.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

/// The Metal backend. This header is plain C++ (the Metal objects live behind State), so it can be
/// included anywhere; the rest of phox/metal needs Objective-C++.
namespace phox::mtl {

class Buffer;
class Device;
class MeshCache;
class PipelineCache;
class ShaderLibrary;
class TransientPool;

class Renderer final : public phox::Renderer {
public:
    /// Frames the CPU may encode ahead of the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    Renderer();
    ~Renderer() override;

    void initialize(const RendererConfig &config) override;
    void cleanup() override;
    void render(const Scene &scene) override;
    const CpuImage &lastFrame() const override { return m_lastFrame; }
    RendererInfo info() const override;
    const std::string &frameGraphDot() const override { return m_frameGraphDot; }

private:
    struct State; // Objective-C objects: the layer and the frames-in-flight semaphore

    void createDevice();
    void createPipelines();
    void createLayer();
    /// Waits for frames in flight, then releases every Metal object. Safe on a partially initialised renderer.
    void releaseMetal();
    void renderFrame(const Scene &scene);
    void renderFallback(const Scene &scene);

    RendererConfig m_config;
    Window *m_window = nullptr;
    std::unique_ptr<State> m_state;
    std::unique_ptr<Device> m_device;
    std::unique_ptr<ShaderLibrary> m_shaders;
    std::unique_ptr<PipelineCache> m_pipelines;
    std::unique_ptr<TransientPool> m_transients;
    std::unique_ptr<MeshCache> m_meshes;
    std::unique_ptr<Buffer> m_readback; ///< headless only
    Format m_drawableFormat = Format::BGRA8Unorm;
    std::uint64_t m_frameNumber = 0;

    CpuImage m_lastFrame;
    std::string m_frameGraphDot;
    std::string m_deviceName;
    /// Command buffers that completed with an error (counted from Metal's completion handlers).
    std::atomic<std::uint32_t> m_gpuErrors{0};
    bool m_validation = false;
    bool m_initialized = false;
    bool m_useFallback = false;
};

} // namespace phox::mtl

#endif // PHOX_METAL_RENDERER_HPP
