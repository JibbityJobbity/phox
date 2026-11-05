// Maps the HDR scene colour to the display range: MSL counterpart of tonemap.frag. Mirrors
// phox::applyToneMapping.
#include "common.h"

struct TonemapPushConstants {
    float exposure;
    uint op;         // 0 clamp, 1 Reinhard, 2 ACES (ToneMapOperator)
    uint encodeSrgb; // 1: apply the sRGB transfer function here; 0: the target's format does it
    uint padding;
};

// Mirrors phox::linearToSrgb.
inline float3 linearToSrgb(float3 c) {
    return select(c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c > float3(0.0031308));
}

fragment float4 tonemap_frag(float4 position [[position]],
                             texture2d<float, access::read> hdrColor [[texture(PHOX_TEXTURE)]], // same size as the target
                             constant TonemapPushConstants &tonemap [[buffer(PHOX_DRAW_BUFFER)]]) {
    float3 c = hdrColor.read(uint2(position.xy)).rgb * tonemap.exposure;
    if (tonemap.op == 1u) {
        c = c / (1.0 + c);
    } else if (tonemap.op == 2u) {
        c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    }
    c = clamp(c, 0.0, 1.0);
    return float4(tonemap.encodeSrgb != 0u ? linearToSrgb(c) : c, 1.0);
}
