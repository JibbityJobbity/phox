#ifndef PHOX_VULKAN_SHADER_INTERFACE_HPP
#define PHOX_VULKAN_SHADER_INTERFACE_HPP

#include "phox/ShaderInterface.hpp"

#include <cstdint>

/// The shader data blocks, shared by every backend (phox/ShaderInterface.hpp). In GLSL they are declared
/// in shaders/common.glsl: FrameUniforms is set 0, binding 0 (SetLayout::Frame), DrawPushConstants the
/// push constant block. The meshlet path's blocks below are Vulkan-only.
namespace phox::vk {

using phox::DrawPushConstants;
using phox::FrameUniforms;
using phox::MaterialData;
using phox::TonemapPushConstants;

/// MeshletDrawPushConstants::flags.
inline constexpr std::uint32_t kMeshletFrustumCulling = 1u << 0;
inline constexpr std::uint32_t kMeshletConeCulling = 1u << 1;
/// Meshlets per task shader workgroup (PHOX_TASK_GROUP_SIZE in shaders/common.glsl).
inline constexpr std::uint32_t kMeshletTaskGroupSize = 32;

/// Push constants for one draw on the meshlet path: task and mesh stages (the mesh shader passes the
/// material index on to the fragment shader). At most 128 bytes, the smallest maximum Vulkan allows.
struct MeshletDrawPushConstants {
    mat4 model{1.0f};
    vec4 cameraPosition{0.0f};  ///< xyz: the camera in the mesh's object space, for cone culling
    std::uint64_t vertices = 0; ///< device address of the mesh's Vertex array
    /// device address of its Meshlet array, followed by the meshlet data that the meshlets' offsets index
    std::uint64_t meshlets = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t flags = 0;            ///< kMeshletFrustumCulling | kMeshletConeCulling
    std::uint32_t visibilityOffset = 0; ///< occlusion culling: the draw's first word of visibility bits
    std::uint32_t material = 0;         ///< index into the frame's MaterialData array
    /// Cluster LOD: the error, in pixels, a meshlet may show (phox::lodSelected). Meshlets without a
    /// hierarchy (level 0, no parent) pass whatever it is.
    float lodThreshold = 1.0f;
    std::uint32_t padding[3]{};
};
static_assert(sizeof(MeshletDrawPushConstants) == 128, "must match MeshletDrawPushConstants in shaders/common.glsl");

/// Tiles of a generated heightfield: cells a side (PHOX_GRID_TILE in shaders/forward_grid.glsl). 8 cells are
/// 81 vertices and 128 triangles, within every mesh shader's limits, and divisible by the coarsest stride.
inline constexpr std::uint32_t kGridTileCells = 8;

/// GridDrawPushConstants::flags.
inline constexpr std::uint32_t kGridFrustumCulling = 1u << 0;

/// Push constants of one generated heightfield drawn with shaders/forward_grid.task and .mesh (task and mesh
/// stages; the mesh shader passes the material index on to the fragment shader).
struct GridDrawPushConstants {
    mat4 model{1.0f};
    std::uint64_t heights = 0;  ///< device address of the float heights, resolution * resolution
    std::uint32_t resolution = 0;
    float size = 1.0f;
    std::uint32_t flags = 0;    ///< kGridFrustumCulling
    std::uint32_t material = 0; ///< index into the frame's MaterialData array
    float lodThreshold = 1.0f;  ///< the widest, in pixels, a merged cell may be
    std::uint32_t padding = 0;
};
static_assert(sizeof(GridDrawPushConstants) == 96, "must match GridDrawPushConstants in shaders/forward_grid.glsl");

/// Push constants of the depth pyramid build (shaders/depthpyramid.comp), compute stage.
struct DepthPyramidPushConstants {
    std::uint32_t level = 0; ///< the mip written
};
static_assert(sizeof(DepthPyramidPushConstants) == 4, "must match shaders/depthpyramid.comp");

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
