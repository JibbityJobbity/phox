#ifndef PHOX_METAL_PIPELINE_CACHE_HPP
#define PHOX_METAL_PIPELINE_CACHE_HPP

#include "phox/metal/MetalCommon.hpp"
#include "phox/pipeline/Pipeline.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

#include <memory>
#include <unordered_map>

namespace phox::mtl {

class Device;
class ShaderLibrary;

/// A render pipeline state plus the state Metal keeps outside it (depth-stencil, culling, winding).
class Pipeline final : public phox::Pipeline {
public:
    Pipeline(PipelineDesc desc, id<MTLRenderPipelineState> state, id<MTLDepthStencilState> depthStencil);
    ~Pipeline() override;

    id<MTLRenderPipelineState> state() const { return m_state; }
    id<MTLDepthStencilState> depthStencil() const { return m_depthStencil; }
    MTLPrimitiveType primitive() const;
    /// Sets the pipeline state, depth-stencil state, cull mode and front-face winding on @p encoder.
    void bind(id<MTLRenderCommandEncoder> encoder) const;

private:
    id<MTLRenderPipelineState> m_state;
    id<MTLDepthStencilState> m_depthStencil;
};

/// Creates pipelines from descriptions on first use and keeps them. The descriptions are the same ones
/// the Vulkan backend uses; set layouts and push constants map to fixed indices (metal/ShaderInterface.hpp).
class PipelineCache {
public:
    PipelineCache(Device &device, ShaderLibrary &shaders);
    ~PipelineCache();
    PipelineCache(const PipelineCache &) = delete;
    PipelineCache &operator=(const PipelineCache &) = delete;

    /// The pipeline for @p desc, created if this is the first request for it.
    /// @throws std::runtime_error if a shader is missing or creation fails
    const Pipeline &get(const PipelineDesc &desc);
    std::size_t size() const { return m_pipelines.size(); }

private:
    std::unique_ptr<Pipeline> create(const PipelineDesc &desc);

    Device &m_device;
    ShaderLibrary &m_shaders;
    std::unordered_map<PipelineDesc, std::unique_ptr<Pipeline>, PipelineDescHash> m_pipelines;
};

} // namespace phox::mtl

#endif // PHOX_METAL_PIPELINE_CACHE_HPP
