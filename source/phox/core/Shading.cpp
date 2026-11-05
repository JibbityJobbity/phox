#include "phox/Shading.hpp"

#include "phox/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace phox {

namespace {

constexpr float kPi = 3.14159265358979f;

float distributionGgx(float nDotH, float alpha) {
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float visibilitySmithGgx(float nDotL, float nDotV, float alpha) {
    const float a2 = alpha * alpha;
    const float ggxV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float ggxL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    const float sum = ggxV + ggxL;
    return sum > 0.0f ? 0.5f / sum : 0.0f;
}

vec3 fresnelSchlick(const vec3 &f0, float vDotH) {
    const float f = std::pow(1.0f - vDotH, 5.0f);
    return f0 + (vec3(1.0f) - f0) * f;
}

vec4 sampleOr(const std::shared_ptr<const Texture> &texture, const vec2 &uv, const vec4 &fallback) {
    return texture ? texture->sample(uv) : fallback;
}

} // namespace

vec3 shadePbr(const SurfaceSample &s, const vec3 &n, const vec3 &v, const vec3 &l, const vec3 &lightColor,
              float ambient) {
    const float roughness = std::clamp(s.roughness, 0.045f, 1.0f);
    const float alpha = roughness * roughness;
    const vec3 f0 = glm::mix(vec3(0.04f), s.baseColor, s.metallic);
    const vec3 diffuseColor = s.baseColor * (1.0f - s.metallic);

    const vec3 h = glm::normalize(v + l);
    const float nDotL = std::clamp(glm::dot(n, l), 0.0f, 1.0f);
    const float nDotV = std::max(std::abs(glm::dot(n, v)), 1e-4f);
    const float nDotH = std::clamp(glm::dot(n, h), 0.0f, 1.0f);
    const float vDotH = std::clamp(glm::dot(v, h), 0.0f, 1.0f);

    const vec3 f = fresnelSchlick(f0, vDotH);
    const vec3 specular = f * (distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotL, nDotV, alpha));
    const vec3 diffuse = (vec3(1.0f) - f) * diffuseColor / kPi;
    const vec3 direct = (diffuse + specular) * lightColor * (kPi * nDotL);
    const vec3 indirect = ambient * lightColor * (diffuseColor + f0) * s.occlusion;
    return direct + indirect + s.emissive;
}

ShadedFragment shadeMaterial(const Material &m, const vec3 &position, vec3 normal, const vec4 &tangent, const vec2 &uv,
                             bool frontFacing, const vec3 &cameraPosition, const vec3 &lightDirection,
                             const vec3 &lightColor, float ambient) {
    ShadedFragment result;
    const vec4 baseColor = m.albedo * sampleOr(m.baseColorTexture, uv, vec4(1.0f));
    if (m.alphaMode == AlphaMode::Mask && baseColor.a < m.alphaCutoff) {
        result.discarded = true;
        return result;
    }

    normal = glm::normalize(normal);
    if (m.normalTexture) {
        vec3 t = vec3(tangent) - normal * glm::dot(normal, vec3(tangent));
        const float length2 = glm::dot(t, t);
        if (length2 > 1e-12f) {
            t /= std::sqrt(length2);
            const vec3 b = glm::cross(normal, t) * tangent.w;
            vec3 mapped = vec3(m.normalTexture->sample(uv)) * 2.0f - 1.0f;
            mapped.x *= m.normalScale;
            mapped.y *= m.normalScale;
            normal = glm::normalize(mapped.x * t + mapped.y * b + mapped.z * normal);
        }
    }
    if (!frontFacing) {
        normal = -normal;
    }

    SurfaceSample s;
    s.baseColor = vec3(baseColor);
    const vec4 metallicRoughness = sampleOr(m.metallicRoughnessTexture, uv, vec4(1.0f));
    s.metallic = m.metallic * metallicRoughness.b;
    s.roughness = m.roughness * metallicRoughness.g;
    s.occlusion = 1.0f + m.occlusionStrength * (sampleOr(m.occlusionTexture, uv, vec4(1.0f)).r - 1.0f);
    s.emissive = m.emissive * vec3(sampleOr(m.emissiveTexture, uv, vec4(1.0f)));

    const vec3 view = glm::normalize(cameraPosition - position);
    result.color = shadePbr(s, normal, view, lightDirection, lightColor, ambient);
    result.alpha = m.alphaMode == AlphaMode::Blend ? baseColor.a : 1.0f;
    return result;
}

} // namespace phox
