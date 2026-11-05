#ifndef PHOX_METAL_PASSES_FORWARD_PASS_HPP
#define PHOX_METAL_PASSES_FORWARD_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/ShaderInterface.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

namespace phox::mtl {

class MeshCache;
class PipelineCache;

struct ForwardPassOutput {
    ImageHandle color;
    ImageHandle depth;
};

/// The pipeline the forward pass uses for the given attachment formats (the same description as the
/// Vulkan backend's).
PipelineDesc forwardPipelineDesc(Format color, Format depth);

/// Clears @p color to the view's background and @p depth to the far plane, then draws every item in
/// @p view lit by its directional light. @p frame is bound at kFrameBufferIndex. The meshes must have
/// been uploaded with MeshCache::prepare.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const FrameUniforms &frame,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines);

} // namespace phox::mtl

#endif // PHOX_METAL_PASSES_FORWARD_PASS_HPP
