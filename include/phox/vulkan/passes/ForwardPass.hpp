#ifndef PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
#define PHOX_VULKAN_PASSES_FORWARD_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/GpuScene.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>

namespace phox::vk {

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

/// Which instance cull shader a cull pipeline runs (see shaders/cull.glsl).
enum class CullVariant : std::uint8_t {
    Meshlets, ///< cull.comp: frustum, work items for the task shader
    Early,    ///< cull_early.comp: instances visible last frame
    Late,     ///< cull_late.comp: the rest of the frustum, against the depth pyramid; rewrites the history
    Vertex,   ///< cull_vertex.comp: frustum, indirect draw commands
};

/// The pipelines of the GPU-driven forward pass, for the given attachment formats: drawing with vertex
/// shaders (vertices fetched by the shader, from indirect draws), or meshlets with task and mesh shaders
/// (indirect task shader dispatches). @p cull is Back for single-sided materials; @p blend alpha blends
/// without writing depth (AlphaMode::Blend). Blended meshlets use MeshletPhase::Single.
PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None, bool blend = false);
PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull = CullMode::None,
                                        MeshletPhase phase = MeshletPhase::Single, bool blend = false);
PipelineDesc cullPipelineDesc(CullVariant variant);
PipelineDesc drawArgsPipelineDesc();

/// The descriptor sets every forward pipeline binds first.
struct ForwardSets {
    VkDescriptorSet frame = VK_NULL_HANDLE;    ///< set 0: FrameUniforms and the frame's MaterialData table
    VkDescriptorSet textures = VK_NULL_HANDLE; ///< set 1: the texture table (TextureCache::set())
};

/// How the task shader picks meshlets (the meshlet path only).
struct MeshletSettings {
    /// Skip objects and meshlets outside the frustum, and (single-sided only) meshlets facing away.
    bool culling = true;
    /// Cluster LOD: the error, in pixels, a meshlet may show (meshes uploaded with hierarchies only).
    float lodErrorPixels = 1.0f;
};

/// Two-phase occlusion culling (the meshlet path only): GpuScene's state buffers, imported into the
/// graph with their device addresses bound (initial usage Undefined when GpuScene::Frame::clearState).
struct OcclusionState {
    BufferHandle history;
    BufferHandle visibility;
};

/// The GPU-driven forward pass. Clears @p color to the view's background and @p depth to the far plane,
/// then draws @p scene, every object of the view, with no draw recorded per object:
///
/// - "cull": a compute pass over every instance slot tests the frustum and appends what survives to a
///   draw list, with work per bucket (pipeline);
/// - "draw args" (meshlet path): turns the counts into indirect task shader dispatch arguments;
/// - "forward": per bucket one indirect dispatch (meshlet path), or one indirect draw with a count from
///   the GPU (vertex path).
///
/// With @p occlusion, that runs twice around a depth pyramid: "cull early" keeps the objects visible at
/// the end of the last frame, and "forward early" draws their meshlets that were visible; "cull late"
/// tests every other object in the frustum against the pyramid, and "forward late" draws the meshlets the
/// early pass did not that are not hidden, recording what is visible for the next frame. Needs D32Float
/// depth.
///
/// Then "forward blend" draws the scene's blended objects over the result, in the order GpuScene wrote them.
ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const ForwardSets &sets,
                                 const GpuScene::Frame &scene, ImageHandle color, ImageHandle depth, bool meshlets,
                                 PipelineCache &pipelines, const MeshletSettings &settings = {},
                                 const OcclusionState *occlusion = nullptr);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_FORWARD_PASS_HPP
