#ifndef PHOX_SHADING_HPP
#define PHOX_SHADING_HPP

#include "phox/Math.hpp"

namespace phox {

struct Material;

/// A material evaluated at one point: its factors times its textures.
struct SurfaceSample {
    vec3 baseColor{1.0f}; ///< linear
    float metallic = 0.0f;
    float roughness = 0.5f; ///< perceptual
    float occlusion = 1.0f; ///< 1: unoccluded
    vec3 emissive{0.0f};
};

/// Outgoing radiance towards the eye, by the metallic-roughness BRDF of glTF 2.0. @p n, @p v and @p l are
/// unit vectors: the shading normal, towards the eye and towards the light. The light colour is irradiance
/// / pi, so a white Lambertian surface facing a white light reflects 1; @p ambient lights every surface
/// evenly. The same as shadePbr in shaders/pbr.glsl, which the GPU runs.
vec3 shadePbr(const SurfaceSample &surface, const vec3 &n, const vec3 &v, const vec3 &l, const vec3 &lightColor,
              float ambient);

/// What forward.frag computes for @p material at one point, given the interpolated vertex attributes
/// (world space): the lit colour and the alpha it writes. Textures are sampled bilinearly from their
/// first mip (Texture::sample), where the GPU filters between mips.
struct ShadedFragment {
    vec3 color{0.0f};
    float alpha = 1.0f;
    bool discarded = false; ///< below the alpha cutoff of an AlphaMode::Mask material
};
ShadedFragment shadeMaterial(const Material &material, const vec3 &position, vec3 normal, const vec4 &tangent,
                             const vec2 &uv, bool frontFacing, const vec3 &cameraPosition, const vec3 &lightDirection,
                             const vec3 &lightColor, float ambient);

} // namespace phox

#endif // PHOX_SHADING_HPP
