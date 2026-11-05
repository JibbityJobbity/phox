// Maps the HDR scene colour to the display range: MSL counterpart of tonemap.frag. Mirrors
// phox::applyToneMapping.
#include "common.h"

struct TonemapPushConstants {
    float exposure;
    uint op; // 0 clamp, 1 Reinhard, 2 ACES (ToneMapOperator)
};

fragment float4 tonemap_frag(float4 position [[position]],
                             texture2d<float, access::read> hdrColor [[texture(PHOX_TEXTURE)]], // same size as the target
                             constant TonemapPushConstants &tonemap [[buffer(PHOX_DRAW_BUFFER)]]) {
    float3 c = hdrColor.read(uint2(position.xy)).rgb * tonemap.exposure;
    if (tonemap.op == 1u) {
        c = c / (1.0 + c);
    } else if (tonemap.op == 2u) {
        c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    }
    return float4(clamp(c, 0.0, 1.0), 1.0);
}
