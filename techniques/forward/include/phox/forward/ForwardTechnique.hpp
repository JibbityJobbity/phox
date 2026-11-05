#ifndef PHOX_FORWARD_FORWARD_TECHNIQUE_HPP
#define PHOX_FORWARD_FORWARD_TECHNIQUE_HPP

#include "phox/forward/DrawMeshes.hpp"
#include "phox/forward/Readback.hpp"
#include "phox/forward/UploadMeshes.hpp"
#include "phox/graph/Technique.hpp"

namespace phox::forward {

/// Upload meshes, draw them into a target, read the target back. It needs no optional GPU features.
///
/// Per frame: `setFrame(...)`, then `build(builder)`, then compile and execute the graph, wait for the
/// ticket, and map `graph.resolve(readbackBuffer())`.
class ForwardTechnique final : public graph::Technique {
  public:
    explicit ForwardTechnique(rhi::Format colorFormat = rhi::Format::RGBA8Unorm) : m_colorFormat(colorFormat) {}

    std::string_view name() const override { return "forward"; }
    std::vector<rhi::Feature> requirements() const override { return {}; }
    void init(rhi::Device &device) override { m_draw.init(device, m_colorFormat); }
    void shutdown(rhi::Device &device) override { m_draw.shutdown(device); }
    void build(graph::GraphBuilder &builder) override;

    /// The scene and camera must stay alive until the graph is built. `target` is an imported texture of
    /// the colour format given to the constructor.
    void setFrame(const scene::Scene &scene, const scene::Camera &camera, graph::GraphTexture target) {
        m_scene = &scene;
        m_camera = &camera;
        m_target = target;
    }

    /// Valid after build(); resolve it through the RenderGraph after compile().
    graph::GraphBuffer readbackBuffer() const { return m_readback; }

  private:
    rhi::Format m_colorFormat;
    UploadMeshes m_upload;
    DrawMeshes m_draw;
    Readback m_readbackComponent;
    const scene::Scene *m_scene = nullptr;
    const scene::Camera *m_camera = nullptr;
    graph::GraphTexture m_target;
    graph::GraphBuffer m_readback;
};

} // namespace phox::forward

#endif // PHOX_FORWARD_FORWARD_TECHNIQUE_HPP
