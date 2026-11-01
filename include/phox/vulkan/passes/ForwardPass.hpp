#ifndef PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
#define PHOX_VULKAN_PASSES_FORWARD_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox::vk {

class MeshCache;
class PipelineCache;

struct ForwardPassOutput {
    ImageHandle color;
    ImageHandle depth;
};

/// The pipelines the forward pass uses for the given attachment formats: drawing with vertex shaders,
/// or meshlets with task and mesh shaders. @p cull is Back for single-sided materials.
PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None);
PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None);

/// Clears @p color to the view's background and @p depth to the far plane, then draws every item in
/// @p view lit by its directional light. @p frameSet is bound as set 0 (FrameUniforms for the view).
/// The meshes must have been uploaded with MeshCache::prepare; if the cache holds meshlets, they are
/// drawn with task and mesh shaders, and @p meshletCulling lets the task shader skip those outside the
/// frustum or (single-sided only) facing away.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, VkDescriptorSet frameSet,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 bool meshletCulling = true);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
