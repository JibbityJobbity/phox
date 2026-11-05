// The lit forward pass: MSL counterpart of forward.vert and forward.frag.
#include "common.h"

struct ForwardVertex {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
};

struct ForwardVaryings {
    float4 position [[position]];
    float3 normal; // world space
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
    out.normal = cofactor * in.normal / det;
    out.position = toMetalClip(frame.viewProj * draw.model * float4(in.position, 1.0));
    return out;
}

fragment float4 forward_frag(ForwardVaryings in [[stage_in]], bool frontFacing [[front_facing]],
                             constant FrameUniforms &frame [[buffer(PHOX_FRAME_BUFFER)]],
                             constant DrawPushConstants &draw [[buffer(PHOX_DRAW_BUFFER)]]) {
    float3 normal = normalize(in.normal);
    if (!frontFacing) {
        normal = -normal; // light single-sided geometry from both sides
    }
    const float ambient = frame.lightDirection.w;
    const float diffuse = max(dot(normal, frame.lightDirection.xyz), 0.0);
    const float3 color = draw.albedo.rgb * frame.lightColor.rgb * (ambient + (1.0 - ambient) * diffuse);
    return float4(color, draw.albedo.a);
}
