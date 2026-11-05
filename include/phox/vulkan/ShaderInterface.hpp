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
};
static_assert(sizeof(MeshletDrawPushConstants) == 112, "must match MeshletDrawPushConstants in shaders/common.glsl");

/// Push constants of the depth pyramid build (shaders/depthpyramid.comp), compute stage.
struct DepthPyramidPushConstants {
    std::uint32_t level = 0; ///< the mip written
};
static_assert(sizeof(DepthPyramidPushConstants) == 4, "must match shaders/depthpyramid.comp");

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
