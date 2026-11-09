#ifndef PHOX_VULKAN_SHADER_INTERFACE_HPP
#define PHOX_VULKAN_SHADER_INTERFACE_HPP

#include "phox/ShaderInterface.hpp"

#include <cstdint>

/// The shader data blocks shared by every backend (phox/ShaderInterface.hpp), and those of the Vulkan
/// backend's GPU-driven forward pass. In GLSL they are declared in shaders/common.glsl: FrameUniforms is
/// set 0, binding 0 (SetLayout::Frame), and the frame's MaterialData array binding 1.
namespace phox::vk {

using phox::FrameUniforms;
using phox::MaterialData;
using phox::TonemapPushConstants;

// ---- The GPU-driven scene (shaders/common.glsl) ------------------------------------------------------
// Addresses are buffer device addresses (shaders read them as uvec2).

inline constexpr std::uint32_t kNoMesh = 0xFFFFFFFFu;
/// GpuInstance::flags: single-sided and not mirrored, drawn by the pipeline that culls back faces.
inline constexpr std::uint32_t kInstanceCullBackFaces = 1u << 0;

/// One scene object: an entry of the frame's instance array, indexed by the object's slot (GpuScene).
struct GpuInstance {
    mat4 model{1.0f};
    /// Index into the frame's GpuMesh array. kNoMesh: an empty slot, or an object the CPU draws (blended).
    std::uint32_t mesh = kNoMesh;
    std::uint32_t flags = 0;            ///< kInstanceCullBackFaces
    std::uint32_t visibilityOffset = 0; ///< first word of the slot's meshlet visibility bits
    std::uint32_t padding = 0;
};
static_assert(sizeof(GpuInstance) == 80, "must match GpuInstance in shaders/common.glsl");

/// One mesh of the frame (MeshCache's buffers).
struct GpuMesh {
    vec4 sphere{0.0f};            ///< object space: xyz centre, w radius, enclosing the mesh
    std::uint64_t vertices = 0;   ///< its phox::Vertex array
    std::uint64_t meshlets = 0;   ///< meshlet path: its Meshlet array, followed by their data
    std::uint32_t meshletCount = 0; ///< of every LOD level
    std::uint32_t indexCount = 0;   ///< vertex path
    std::uint32_t firstIndex = 0;   ///< vertex path: where its indices start in MeshCache's index pool
    std::uint32_t padding = 0;
};
static_assert(sizeof(GpuMesh) == 48, "must match GpuMesh in shaders/common.glsl");

/// GpuDraw::flags.
inline constexpr std::uint32_t kDrawConeCulling = 1u << 0; ///< single-sided: meshlets facing away may be skipped
inline constexpr std::uint32_t kDrawEarly = 1u << 1;       ///< the early pass drew it (occlusion culling)

/// One instance a cull pass decided to draw (or, for blended objects, the CPU did).
struct GpuDraw {
    mat4 model{1.0f};
    vec4 cameraPosition{0.0f}; ///< xyz: the camera in the instance's object space
    std::uint64_t vertices = 0;
    std::uint64_t primitives = 0;     ///< meshlet path: GpuMesh::meshlets; unused on the vertex path
    std::uint32_t count = 0;          ///< meshlets, or indices
    std::uint32_t material = 0;       ///< index into the frame's MaterialData array (the object's slot)
    std::uint32_t visibilityOffset = 0;
    std::uint32_t flags = 0;          ///< kDrawConeCulling | kDrawEarly
};
static_assert(sizeof(GpuDraw) == 112, "must match GpuDraw in shaders/common.glsl");

/// What one cull pass produced, per bucket (0: both faces drawn, 1: back faces culled), and the indirect
/// arguments of the task shader dispatch for each (the args pass fills them in).
struct DrawCounters {
    std::uint32_t drawCount = 0;
    std::uint32_t workCount[2]{}; ///< work items (meshlet path) or VkDrawIndexedIndirectCommands (vertex path)
    std::uint32_t padding = 0;
    std::uint32_t args[2][4]{};   ///< VkDrawMeshTasksIndirectCommandEXT (xyz) per bucket
};
static_assert(sizeof(DrawCounters) == 48, "must match CounterBuffer in shaders/common.glsl");
inline constexpr std::uint32_t kDrawCountersWorkOffset = 4; ///< byte offset of workCount[0]
inline constexpr std::uint32_t kDrawCountersArgsOffset = 16; ///< byte offset of args[0]

/// Meshlets per work item (PHOX_TASK_GROUP_SIZE in shaders/common.glsl): one task shader workgroup.
inline constexpr std::uint32_t kMeshletTaskGroupSize = 32;

/// CullPushConstants::flags.
inline constexpr std::uint32_t kCullFrustum = 1u << 0;

/// Push constants of the instance cull pass (shaders/cull.glsl), compute stage.
struct CullPushConstants {
    std::uint64_t instances = 0; ///< GpuInstance[]
    std::uint64_t meshes = 0;    ///< GpuMesh[]
    std::uint64_t draws = 0;     ///< GpuDraw[], appended to
    std::uint64_t work = 0;      ///< work items or draw commands, appended to per bucket
    std::uint64_t counters = 0;  ///< DrawCounters
    std::uint64_t history = 0;   ///< per slot: visible at the end of the previous frame (occlusion culling)
    std::uint32_t instanceCount = 0;
    std::uint32_t workCapacity = 0; ///< entries per bucket
    std::uint32_t flags = 0;        ///< kCullFrustum
    std::uint32_t padding = 0;
};
static_assert(sizeof(CullPushConstants) == 64, "must match CullPushConstants in shaders/common.glsl");

/// Push constants of the draw arguments pass (shaders/draw_args.comp), compute stage.
struct ArgsPushConstants {
    std::uint64_t counters = 0;
    std::uint32_t maxGroupsX = 0;
    std::uint32_t padding = 0;
};
static_assert(sizeof(ArgsPushConstants) == 16, "must match ArgsPushConstants in shaders/common.glsl");

/// MeshletDrawPushConstants::flags.
inline constexpr std::uint32_t kMeshletFrustumCulling = 1u << 0;
inline constexpr std::uint32_t kMeshletConeCulling = 1u << 1;

/// Push constants of one indirect task shader dispatch (task and mesh stages): the work list to run.
struct MeshletDrawPushConstants {
    std::uint64_t draws = 0;      ///< GpuDraw[]
    std::uint64_t work = 0;       ///< work items; bucket b's start at b * workCapacity
    std::uint64_t counters = 0;   ///< DrawCounters
    std::uint64_t visibility = 0; ///< meshlet visibility words (occlusion culling)
    std::uint32_t bucket = 0;
    std::uint32_t workCapacity = 0;
    std::uint32_t flags = 0;      ///< kMeshletFrustumCulling | kMeshletConeCulling
    /// Cluster LOD: the error, in pixels, a meshlet may show (phox::lodSelected). Meshlets without a
    /// hierarchy (level 0, no parent) pass whatever it is.
    float lodThreshold = 1.0f;
};
static_assert(sizeof(MeshletDrawPushConstants) == 48, "must match MeshletDrawPushConstants in shaders/common.glsl");

/// Push constants of the vertex path's indirect draws (vertex stage).
struct VertexDrawPushConstants {
    std::uint64_t draws = 0; ///< GpuDraw[]; gl_InstanceIndex (the command's firstInstance) picks one
    std::uint64_t padding = 0;
};
static_assert(sizeof(VertexDrawPushConstants) == 16, "must match VertexDrawPushConstants in shaders/common.glsl");

/// Push constants of the depth pyramid build (shaders/depthpyramid.comp), compute stage.
struct DepthPyramidPushConstants {
    std::uint32_t level = 0; ///< the mip written
};
static_assert(sizeof(DepthPyramidPushConstants) == 4, "must match shaders/depthpyramid.comp");

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
