#ifndef PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
#define PHOX_VULKAN_PASSES_FORWARD_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/MeshletVisibility.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>

namespace phox::vk {

class MeshCache;
class PipelineCache;

struct ForwardPassOutput {
    ImageHandle color;
    ImageHandle depth;
};

/// Which task shader a meshlet pipeline runs (see shaders/forward_task.glsl).
enum class MeshletPhase : std::uint8_t {
    Single, ///< one pass, frustum and cone culling only
    Early,  ///< occlusion culling, first pass: the meshlets visible last frame
    Late,   ///< occlusion culling, second pass: the rest, tested against the depth pyramid
};

/// The pipelines the forward pass uses for the given attachment formats: drawing with vertex shaders,
/// or meshlets with task and mesh shaders. @p cull is Back for single-sided materials.
PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None);
PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None,
                                        MeshletPhase phase = MeshletPhase::Single);

/// Two-phase occlusion culling of meshlets (the meshlet path only).
struct MeshletOcclusion {
    BufferHandle visibility;                         ///< MeshletVisibility's buffer, imported
    const MeshletVisibility::Frame *frame = nullptr; ///< its offsets for the view's draws
};

/// Clears @p color to the view's background and @p depth to the far plane, then draws every item in
/// @p view lit by its directional light. @p frameSet is bound as set 0 (FrameUniforms for the view).
/// The meshes must have been uploaded with MeshCache::prepare; if the cache holds meshlets, they are
/// drawn with task and mesh shaders, and @p meshletCulling lets the task shader skip those outside the
/// frustum or (single-sided only) facing away.
///
/// With @p occlusion, the meshlets are drawn in two passes: "forward early" draws those visible last
/// frame, a depth pyramid is built from its depth, and "forward late" draws the rest that are not hidden
/// behind it, recording which meshlets are visible for the next frame. Needs D32Float depth.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, VkDescriptorSet frameSet,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 bool meshletCulling = true, const MeshletOcclusion *occlusion = nullptr);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
