#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#define PHOX_MATERIALS
#include "common.glsl"
#include "pbr.glsl"

layout(location = 0) in vec3 inPosition; // world space
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) flat in uint inMaterial;

layout(location = 0) out vec4 outColor;

vec4 sampleTexture(uint index, vec2 uv, vec4 fallback) {
    return index == kNoTexture ? fallback : texture(textures[nonuniformEXT(index)], uv);
}

void main() {
    MaterialData m = materials[inMaterial];
    vec4 baseColor = m.baseColor * sampleTexture(m.baseColorTexture, inTexCoord, vec4(1.0));
    if ((m.flags & kMaterialAlphaMask) != 0u && baseColor.a < m.alphaCutoff) {
        discard;
    }

    vec3 normal = normalize(inNormal);
    if (m.normalTexture != kNoTexture) {
        vec3 tangent = inTangent.xyz - normal * dot(normal, inTangent.xyz);
        float length2 = dot(tangent, tangent);
        if (length2 > 1e-12) {
            tangent *= inversesqrt(length2);
            vec3 bitangent = cross(normal, tangent) * inTangent.w;
            vec3 t = texture(textures[nonuniformEXT(m.normalTexture)], inTexCoord).xyz * 2.0 - 1.0;
            t.xy *= m.normalScale;
            normal = normalize(t.x * tangent + t.y * bitangent + t.z * normal);
        }
    }
    if (!gl_FrontFacing) {
        normal = -normal; // light single-sided geometry from both sides
    }

    SurfaceSample s;
    s.baseColor = baseColor.rgb;
    vec4 metallicRoughness = sampleTexture(m.metallicRoughnessTexture, inTexCoord, vec4(1.0));
    s.metallic = m.metallic * metallicRoughness.b;
    s.roughness = m.roughness * metallicRoughness.g;
    s.occlusion = 1.0 + m.occlusionStrength * (sampleTexture(m.occlusionTexture, inTexCoord, vec4(1.0)).r - 1.0);
    s.emissive = m.emissive.rgb * sampleTexture(m.emissiveTexture, inTexCoord, vec4(1.0)).rgb;

    vec3 view = normalize(frame.cameraPosition.xyz - inPosition);
    vec3 color = shadePbr(s, normal, view, frame.lightDirection.xyz, frame.lightColor.rgb, frame.lightDirection.w);
    float alpha = (m.flags & kMaterialAlphaBlend) != 0u ? baseColor.a : 1.0;
    outColor = vec4(color, alpha);
}
