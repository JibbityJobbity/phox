#ifndef PHOX_SHADER_INTERFACE_HPP
#define PHOX_SHADER_INTERFACE_HPP

#include "phox/Math.hpp"

#include <cstdint>

/// C++ mirrors of the data blocks the scene shaders read. Every backend's shaders declare the same
/// layout: shaders/common.glsl (Vulkan) and shaders/metal/common.h (Metal). Keep all three in sync.
namespace phox {

/// Per-frame data (SetLayout::Frame). std140-compatible: only mat4 and vec4 members.
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
static_assert(sizeof(FrameUniforms) == 352, "must match FrameUniforms in the shaders");

/// Per-draw constants, visible to the vertex and fragment stages. albedo comes first to dodge an
/// llvmpipe bug (see shaders/common.glsl).
struct DrawPushConstants {
    vec4 albedo{1.0f};
    mat4 model{1.0f};
};
static_assert(sizeof(DrawPushConstants) == 80, "must match DrawPushConstants in the shaders");

/// Constants of the tonemap pass (tonemap.frag / tonemap.metal), fragment stage.
struct TonemapPushConstants {
    float exposure = 1.0f;
    std::uint32_t op = 0; ///< ToneMapOperator
};
static_assert(sizeof(TonemapPushConstants) == 8, "must match the tonemap shaders");

} // namespace phox

#endif // PHOX_SHADER_INTERFACE_HPP
