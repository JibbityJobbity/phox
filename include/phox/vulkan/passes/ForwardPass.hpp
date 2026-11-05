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
/// or meshlets with task and mesh shaders. @p cull is Back for single-sided materials; @p blend alpha
/// blends without writing depth (AlphaMode::Blend). Blended meshlets use MeshletPhase::Single.
PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None, bool blend = false);
PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None,
                                        MeshletPhase phase = MeshletPhase::Single, bool blend = false);

/// The descriptor sets every forward pipeline binds first.
struct ForwardSets {
    VkDescriptorSet frame = VK_NULL_HANDLE;    ///< set 0: FrameUniforms and the frame's MaterialData table
    VkDescriptorSet textures = VK_NULL_HANDLE; ///< set 1: the texture table (TextureCache::set())
};

/// Two-phase occlusion culling of meshlets (the meshlet path only).
struct MeshletOcclusion {
    BufferHandle visibility;                         ///< MeshletVisibility's buffer, imported
    const MeshletVisibility::Frame *frame = nullptr; ///< its offsets for the view's draws
};

/// Clears @p color to the view's background and @p depth to the far plane, then draws every item in
/// @p view lit by its directional light: view.draws, then view.transparent blended over them ("forward
/// blend"). The material table in @p sets has one entry per item, view.draws' first. The meshes must
/// have been uploaded with MeshCache::prepare, and the textures with TextureCache::prepare; if the mesh
/// cache holds meshlets, they are drawn with task and mesh shaders, and @p meshletCulling lets the task
/// shader skip those outside the frustum or (single-sided only) facing away.
///
/// With @p occlusion, the meshlets are drawn in two passes: "forward early" draws those visible last
/// frame, a depth pyramid is built from its depth, and "forward late" draws the rest that are not hidden
/// behind it, recording which meshlets are visible for the next frame. Needs D32Float depth.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const ForwardSets &sets,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 bool meshletCulling = true, const MeshletOcclusion *occlusion = nullptr);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
