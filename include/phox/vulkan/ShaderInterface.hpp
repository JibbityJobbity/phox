#ifndef PHOX_VULKAN_SHADER_INTERFACE_HPP
#define PHOX_VULKAN_SHADER_INTERFACE_HPP

#include "phox/Math.hpp"

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
};
static_assert(sizeof(FrameUniforms) == 256, "must match FrameUniforms in shaders/common.glsl");

/// Push constants for one draw, visible to the vertex and fragment stages. albedo comes first to dodge
/// an llvmpipe bug (see shaders/common.glsl).
struct DrawPushConstants {
    vec4 albedo{1.0f};
    mat4 model{1.0f};
};
static_assert(sizeof(DrawPushConstants) == 80, "must match DrawPushConstants in shaders/common.glsl");

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
