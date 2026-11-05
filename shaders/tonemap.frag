#version 450

// Maps the HDR scene colour to the display range. Mirrors phox::applyToneMapping.

layout(set = 0, binding = 0) uniform sampler2D hdrColor; // same size as the target

layout(push_constant) uniform TonemapPushConstants {
    float exposure;
    uint op;         // 0 clamp, 1 Reinhard, 2 ACES (ToneMapOperator)
    uint encodeSrgb; // 1: apply the sRGB transfer function here; 0: the target's format does it
} tonemap;

// Mirrors phox::linearToSrgb.
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, greaterThan(c, vec3(0.0031308)));
}

layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texelFetch(hdrColor, ivec2(gl_FragCoord.xy), 0).rgb * tonemap.exposure;
    if (tonemap.op == 1u) {
        c = c / (1.0 + c);
    } else if (tonemap.op == 2u) {
        c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    }
    c = clamp(c, 0.0, 1.0);
    outColor = vec4(tonemap.encodeSrgb != 0u ? linearToSrgb(c) : c, 1.0);
}
