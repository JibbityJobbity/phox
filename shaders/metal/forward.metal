// The lit forward pass: MSL counterpart of forward.vert and forward.frag.
#include "common.h"
#include "pbr.h"

struct ForwardVertex {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float4 tangent [[attribute(3)]];
};

struct ForwardVaryings {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal; // world space
    float2 texCoord;
    float4 tangent; // world space xyz, handedness w
};

vertex ForwardVaryings forward_vert(ForwardVertex in [[stage_in]],
                                    constant FrameUniforms &frame [[buffer(PHOX_FRAME_BUFFER)]],
                                    constant DrawPushConstants &draw [[buffer(PHOX_DRAW_BUFFER)]]) {
    // Inverse-transpose keeps normals perpendicular under non-uniform scale. MSL has no inverse(), but
    // for M = [a b c], inverse(M)^T = [b x c, c x a, a x b] / det(M).
    const float3 a = draw.model[0].xyz;
    const float3 b = draw.model[1].xyz;
    const float3 c = draw.model[2].xyz;
    const float3x3 cofactor = float3x3(cross(b, c), cross(c, a), cross(a, b));
    const float det = dot(a, cofactor[0]);

    ForwardVaryings out;
    const float4 world = draw.model * float4(in.position, 1.0);
    out.worldPosition = world.xyz;
    out.normal = cofactor * in.normal / det;
    out.texCoord = in.texCoord;
    out.tangent = float4(float3x3(a, b, c) * in.tangent.xyz, in.tangent.w);
    out.position = toMetalClip(frame.viewProj * world);
    return out;
}

inline float4 sampleOr(uint index, texture2d<float> image, sampler imageSampler, float2 uv, float4 fallback) {
    return index == kNoTexture ? fallback : image.sample(imageSampler, uv);
}

fragment float4 forward_frag(ForwardVaryings in [[stage_in]], bool frontFacing [[front_facing]],
                             constant FrameUniforms &frame [[buffer(PHOX_FRAME_BUFFER)]],
                             constant MaterialData &m [[buffer(PHOX_MATERIAL_BUFFER)]],
                             texture2d<float> baseColorTexture [[texture(PHOX_BASE_COLOR_TEXTURE)]],
                             texture2d<float> metallicRoughnessTexture [[texture(PHOX_METALLIC_ROUGHNESS_TEXTURE)]],
                             texture2d<float> normalTexture [[texture(PHOX_NORMAL_TEXTURE)]],
                             texture2d<float> occlusionTexture [[texture(PHOX_OCCLUSION_TEXTURE)]],
                             texture2d<float> emissiveTexture [[texture(PHOX_EMISSIVE_TEXTURE)]],
                             sampler baseColorSampler [[sampler(PHOX_BASE_COLOR_TEXTURE)]],
                             sampler metallicRoughnessSampler [[sampler(PHOX_METALLIC_ROUGHNESS_TEXTURE)]],
                             sampler normalSampler [[sampler(PHOX_NORMAL_TEXTURE)]],
                             sampler occlusionSampler [[sampler(PHOX_OCCLUSION_TEXTURE)]],
                             sampler emissiveSampler [[sampler(PHOX_EMISSIVE_TEXTURE)]]) {
    const float2 uv = in.texCoord;
    const float4 baseColor = m.baseColor * sampleOr(m.baseColorTexture, baseColorTexture, baseColorSampler, uv, float4(1.0));
    if ((m.flags & kMaterialAlphaMask) != 0u && baseColor.a < m.alphaCutoff) {
        discard_fragment();
    }

    float3 normal = normalize(in.normal);
    if (m.normalTexture != kNoTexture) {
        float3 tangent = in.tangent.xyz - normal * dot(normal, in.tangent.xyz);
        const float length2 = dot(tangent, tangent);
        if (length2 > 1e-12) {
            tangent *= rsqrt(length2);
            const float3 bitangent = cross(normal, tangent) * in.tangent.w;
            float3 t = normalTexture.sample(normalSampler, uv).xyz * 2.0 - 1.0;
            t.xy *= m.normalScale;
            normal = normalize(t.x * tangent + t.y * bitangent + t.z * normal);
        }
    }
    if (!frontFacing) {
        normal = -normal; // light single-sided geometry from both sides
    }

    SurfaceSample s;
    s.baseColor = baseColor.rgb;
    const float4 metallicRoughness =
        sampleOr(m.metallicRoughnessTexture, metallicRoughnessTexture, metallicRoughnessSampler, uv, float4(1.0));
    s.metallic = m.metallic * metallicRoughness.b;
    s.roughness = m.roughness * metallicRoughness.g;
    s.occlusion = 1.0 + m.occlusionStrength *
                            (sampleOr(m.occlusionTexture, occlusionTexture, occlusionSampler, uv, float4(1.0)).r - 1.0);
    s.emissive = m.emissive.rgb * sampleOr(m.emissiveTexture, emissiveTexture, emissiveSampler, uv, float4(1.0)).rgb;

    const float3 view = normalize(frame.cameraPosition.xyz - in.worldPosition);
    const float3 color = shadePbr(s, normal, view, frame.lightDirection.xyz, frame.lightColor.rgb, frame.lightDirection.w);
    const float alpha = (m.flags & kMaterialAlphaBlend) != 0u ? baseColor.a : 1.0;
    return float4(color, alpha);
}
