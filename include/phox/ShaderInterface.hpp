#ifndef PHOX_SHADER_INTERFACE_HPP
#define PHOX_SHADER_INTERFACE_HPP

#include "phox/Math.hpp"

#include <cstdint>

/// C++ mirrors of the data blocks the scene shaders read. Every backend's shaders declare the same
/// layout: shaders/common.glsl (Vulkan) and shaders/metal/common.h (Metal). Keep all three in sync.
namespace phox {

struct Material;

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

/// Per-draw constants of the vertex shader path.
struct DrawPushConstants {
    mat4 model{1.0f};
    std::uint32_t material = 0; ///< index into the frame's MaterialData array
    std::uint32_t padding[3]{};
};
static_assert(sizeof(DrawPushConstants) == 80, "must match DrawPushConstants in the shaders");

/// MaterialData texture index of a texture the material does not have.
inline constexpr std::uint32_t kNoTexture = 0xFFFFFFFFu;

/// MaterialData::flags.
inline constexpr std::uint32_t kMaterialAlphaMask = 1u << 0;  ///< discard below alphaCutoff
inline constexpr std::uint32_t kMaterialAlphaBlend = 1u << 1; ///< keep alpha for blending

/// One phox::Material as the fragment shaders read it, from an array with one entry per draw of the
/// frame (std430). Texture fields index the backend's texture table, or are kNoTexture.
struct MaterialData {
    vec4 baseColor{1.0f};
    vec4 emissive{0.0f}; ///< rgb; a unused
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    std::uint32_t flags = 0;
    std::uint32_t baseColorTexture = kNoTexture;
    std::uint32_t metallicRoughnessTexture = kNoTexture;
    std::uint32_t normalTexture = kNoTexture;
    std::uint32_t occlusionTexture = kNoTexture;
    std::uint32_t emissiveTexture = kNoTexture;
    std::uint32_t padding = 0;
};
static_assert(sizeof(MaterialData) == 80, "must match MaterialData in the shaders");

/// Constants of the tonemap pass (tonemap.frag / tonemap.metal), fragment stage.
struct TonemapPushConstants {
    float exposure = 1.0f;
    std::uint32_t op = 0; ///< ToneMapOperator
    /// 1: apply the sRGB transfer function in the shader. 0 when the target's format encodes on write.
    std::uint32_t encodeSrgb = 1;
    std::uint32_t padding = 0;
};
static_assert(sizeof(TonemapPushConstants) == 16, "must match the tonemap shaders");

/// @p material's factors and flags. Texture fields are left as kNoTexture for the backend to fill in.
MaterialData materialData(const Material &material);

} // namespace phox

#endif // PHOX_SHADER_INTERFACE_HPP
