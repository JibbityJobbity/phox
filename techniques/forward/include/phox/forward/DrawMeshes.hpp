#ifndef PHOX_FORWARD_DRAW_MESHES_HPP
#define PHOX_FORWARD_DRAW_MESHES_HPP

#include "phox/graph/Component.hpp"
#include "phox/rhi/Device.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace phox::forward {

/// Draws the uploaded vertex stream into `target` with the basic shaders: the target is cleared to
/// (0.1, 0.2, 0.3, 1) first, then one draw call is issued with the albedo as a push constant. With no
/// vertices (an empty scene) the pass only clears.
class DrawMeshes {
  public:
    struct In {
        graph::GraphTexture target;
        graph::GraphBuffer vertices;
        uint32_t vertexCount = 0;
        graph::GraphBuffer camera;
        std::array<float, 4> albedo{1.0f, 0.2f, 0.4f, 1.0f};
    };
    struct Out {
        graph::GraphTexture target;
    };

    /// Creates the shaders and the pipeline. `colorFormat` must match the target texture's format.
    void init(rhi::Device &device, rhi::Format colorFormat);
    void shutdown(rhi::Device &device);

    Out setup(graph::GraphBuilder &builder, const In &in);
    std::string_view name() const { return "DrawMeshes"; }

  private:
    rhi::ShaderHandle m_vertexShader;
    rhi::ShaderHandle m_fragmentShader;
    rhi::PipelineHandle m_pipeline;
};

static_assert(graph::Component<DrawMeshes>);

} // namespace phox::forward

#endif // PHOX_FORWARD_DRAW_MESHES_HPP
