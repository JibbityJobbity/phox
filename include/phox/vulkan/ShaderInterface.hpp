#ifndef PHOX_VULKAN_SHADER_INTERFACE_HPP
#define PHOX_VULKAN_SHADER_INTERFACE_HPP

#include "phox/Math.hpp"

#include <cstdint>

/// C++ mirrors of the data blocks declared in shaders/common.glsl. Keep the two in sync.
namespace phox::vk {

/// Set 0, binding 0 of the scene shaders (SetLayout::Frame). std140: only mat4 and vec4 members.
struct FrameUniforms {
    mat4 view{1.0f};
    mat4 proj{1.0f};
    mat4 viewProj{1.0f};
    vec4 cameraPosition{0.0f};                   ///< xyz: world-space camera position
    vec4 lightDirection{0.0f, 0.0f, 1.0f, 0.0f}; ///< xyz: unit vector towards the light; w: ambient term
    vec4 lightColor{1.0f};                       ///< rgb
    vec4 resolution{0.0f};                       ///< xy: render target size in pixels; zw: 1 / size
    vec4 frustumPlanes[6]{};                     ///< world space, as phox::frustumPlanes
};
static_assert(sizeof(FrameUniforms) == 352, "must match FrameUniforms in shaders/common.glsl");

/// Push constants for one draw, visible to the vertex and fragment stages. albedo comes first to dodge
/// an llvmpipe bug (see shaders/common.glsl).
struct DrawPushConstants {
    vec4 albedo{1.0f};
    mat4 model{1.0f};
};
static_assert(sizeof(DrawPushConstants) == 80, "must match DrawPushConstants in shaders/common.glsl");

/// MeshletDrawPushConstants::flags.
inline constexpr std::uint32_t kMeshletFrustumCulling = 1u << 0;
inline constexpr std::uint32_t kMeshletConeCulling = 1u << 1;
/// Meshlets per task shader workgroup (PHOX_TASK_GROUP_SIZE in shaders/common.glsl).
inline constexpr std::uint32_t kMeshletTaskGroupSize = 32;

/// Push constants for one draw on the meshlet path: task, mesh and fragment stages. Starts like
/// DrawPushConstants, which is all forward.frag reads. At most 128 bytes, the smallest maximum Vulkan allows.
struct MeshletDrawPushConstants {
    vec4 albedo{1.0f};
    mat4 model{1.0f};
    vec4 cameraPosition{0.0f};  ///< xyz: the camera in the mesh's object space, for cone culling
    std::uint64_t vertices = 0; ///< device address of the mesh's Vertex array
    /// device address of its Meshlet array, followed by the meshlet data that the meshlets' offsets index
    std::uint64_t meshlets = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t flags = 0;            ///< kMeshletFrustumCulling | kMeshletConeCulling
    std::uint32_t visibilityOffset = 0; ///< occlusion culling: the draw's first word of visibility bits
    std::uint32_t padding = 0;
};
static_assert(sizeof(MeshletDrawPushConstants) == 128, "must match MeshletDrawPushConstants in shaders/common.glsl");

/// Push constants of the depth pyramid build (shaders/depthpyramid.comp), compute stage.
struct DepthPyramidPushConstants {
    std::uint32_t level = 0; ///< the mip written
};
static_assert(sizeof(DepthPyramidPushConstants) == 4, "must match shaders/depthpyramid.comp");

/// Push constants of the tonemap pass (shaders/tonemap.frag), fragment stage.
struct TonemapPushConstants {
    float exposure = 1.0f;
    std::uint32_t op = 0; ///< ToneMapOperator
};
static_assert(sizeof(TonemapPushConstants) == 8, "must match shaders/tonemap.frag");

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
