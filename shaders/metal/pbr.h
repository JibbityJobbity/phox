// Metallic-roughness shading: the MSL counterpart of shaders/pbr.glsl (and phox::shadePbr). Keep the three
// in step.

constant float kPi = 3.14159265358979;

struct SurfaceSample {
    float3 baseColor; // linear
    float metallic;
    float roughness; // perceptual
    float occlusion; // 1: unoccluded
    float3 emissive;
};

// GGX normal distribution, with alpha = roughness^2.
inline float distributionGgx(float nDotH, float alpha) {
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Height-correlated Smith visibility: G / (4 n.l n.v).
inline float visibilitySmithGgx(float nDotL, float nDotV, float alpha) {
    const float a2 = alpha * alpha;
    const float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    const float sum = ggxV + ggxL;
    return sum > 0.0 ? 0.5 / sum : 0.0;
}

inline float3 fresnelSchlick(float3 f0, float vDotH) {
    const float f = pow(1.0 - vDotH, 5.0);
    return f0 + (1.0 - f0) * f;
}

inline float3 shadePbr(SurfaceSample s, float3 n, float3 v, float3 l, float3 lightColor, float ambient) {
    const float roughness = clamp(s.roughness, 0.045, 1.0);
    const float alpha = roughness * roughness;
    const float3 f0 = mix(float3(0.04), s.baseColor, s.metallic);
    const float3 diffuseColor = s.baseColor * (1.0 - s.metallic);

    const float3 h = normalize(v + l);
    const float nDotL = clamp(dot(n, l), 0.0, 1.0);
    const float nDotV = max(abs(dot(n, v)), 1e-4);
    const float nDotH = clamp(dot(n, h), 0.0, 1.0);
    const float vDotH = clamp(dot(v, h), 0.0, 1.0);

    const float3 f = fresnelSchlick(f0, vDotH);
    const float3 specular = f * (distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotL, nDotV, alpha));
    const float3 diffuse = (1.0 - f) * diffuseColor / kPi;
    const float3 direct = (diffuse + specular) * lightColor * (kPi * nDotL);
    const float3 indirect = ambient * lightColor * (diffuseColor + f0) * s.occlusion;
    return direct + indirect + s.emissive;
}
