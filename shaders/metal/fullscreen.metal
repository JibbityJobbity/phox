// MSL counterpart of fullscreen.vert.
#include "common.h"

// One triangle that covers the whole target: vertices (-1,-1), (3,-1), (-1,3). Draw with 3 vertices
// and no vertex buffer.
vertex float4 fullscreen_vert(uint vertexId [[vertex_id]]) {
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return toMetalClip(float4(corner * 2.0 - 1.0, 0.0, 1.0));
}
