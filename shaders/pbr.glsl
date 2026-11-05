// Metallic-roughness shading (glTF 2.0's BRDF). Mirrored by phox::shadePbr (source/phox/core/Shading.cpp)
// for the software rasterizer and by shaders/metal/pbr.h; keep the three in step.

const float kPi = 3.14159265358979;

struct SurfaceSample {
    vec3 baseColor; // linear
    float metallic;
    float roughness; // perceptual
    float occlusion; // 1: unoccluded
    vec3 emissive;
};

// GGX normal distribution, with alpha = roughness^2.
float distributionGgx(float nDotH, float alpha) {
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Height-correlated Smith visibility: G / (4 n.l n.v).
float visibilitySmithGgx(float nDotL, float nDotV, float alpha) {
    float a2 = alpha * alpha;
    float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    float sum = ggxV + ggxL;
    return sum > 0.0 ? 0.5 / sum : 0.0;
}

vec3 fresnelSchlick(vec3 f0, float vDotH) {
    float f = pow(1.0 - vDotH, 5.0);
    return f0 + (1.0 - f0) * f;
}

// Outgoing radiance towards the eye. @p n, @p v and @p l are unit vectors: the shading normal, towards the
// eye and towards the light. The light's colour is irradiance / pi, so a white Lambertian surface facing a
// white light reflects 1. @p ambient lights every surface evenly (a stand-in for environment light).
vec3 shadePbr(SurfaceSample s, vec3 n, vec3 v, vec3 l, vec3 lightColor, float ambient) {
    float roughness = clamp(s.roughness, 0.045, 1.0);
    float alpha = roughness * roughness;
    vec3 f0 = mix(vec3(0.04), s.baseColor, s.metallic);
    vec3 diffuseColor = s.baseColor * (1.0 - s.metallic);

    vec3 h = normalize(v + l);
    float nDotL = clamp(dot(n, l), 0.0, 1.0);
    float nDotV = max(abs(dot(n, v)), 1e-4);
    float nDotH = clamp(dot(n, h), 0.0, 1.0);
    float vDotH = clamp(dot(v, h), 0.0, 1.0);

    vec3 f = fresnelSchlick(f0, vDotH);
    vec3 specular = f * (distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotL, nDotV, alpha));
    vec3 diffuse = (1.0 - f) * diffuseColor / kPi;
    vec3 direct = (diffuse + specular) * lightColor * (kPi * nDotL);
    vec3 indirect = ambient * lightColor * (diffuseColor + f0) * s.occlusion;
    return direct + indirect + s.emissive;
}
