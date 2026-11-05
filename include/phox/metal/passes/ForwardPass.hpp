#ifndef PHOX_METAL_PASSES_FORWARD_PASS_HPP
#define PHOX_METAL_PASSES_FORWARD_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/ShaderInterface.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

namespace phox::mtl {

class MeshCache;
class PipelineCache;
class TextureCache;

struct ForwardPassOutput {
    ImageHandle color;
    ImageHandle depth;
};

/// The pipeline the forward pass uses for the given attachment formats (the same description as the
/// Vulkan backend's vertex shader path). @p cull is Back for single-sided materials; @p blend alpha
/// blends without writing depth (AlphaMode::Blend).
PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None, bool blend = false);

/// Clears @p color to the view's background and @p depth to the far plane, then draws every item in
/// @p view lit by its directional light: view.draws, then view.transparent blended over them ("forward
/// blend"). @p frame is bound at kFrameBufferIndex, each draw's material at kMaterialBufferIndex and its
/// textures from kMaterialTextureIndex. The meshes and textures must have been uploaded with
/// MeshCache::prepare and TextureCache::prepare.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const FrameUniforms &frame,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, TextureCache &textures,
                                 PipelineCache &pipelines);

} // namespace phox::mtl

#endif // PHOX_METAL_PASSES_FORWARD_PASS_HPP
