#include "phox/forward/ForwardTechnique.hpp"

#include <stdexcept>

namespace phox::forward {

void ForwardTechnique::build(graph::GraphBuilder &builder) {
    if (m_scene == nullptr || m_camera == nullptr || !m_target.valid()) {
        throw std::logic_error("ForwardTechnique::build() called before setFrame()");
    }
    const UploadMeshes::Out uploaded = m_upload.setup(builder, {m_scene, m_camera});
    const DrawMeshes::Out drawn =
        m_draw.setup(builder, {m_target, uploaded.vertices, uploaded.vertexCount, uploaded.camera, uploaded.albedo});
    m_readback = m_readbackComponent.setup(builder, {drawn.target}).pixels;
}

} // namespace phox::forward
