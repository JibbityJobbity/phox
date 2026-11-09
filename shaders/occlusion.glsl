// Occlusion against the depth pyramid, shared by the late task shader (meshlets) and the late instance cull
// pass (objects). The including shader defines PHOX_PYRAMID_SET, the set its pipeline binds
// SetLayout::DepthPyramid at, and has `frame` (common.glsl).

// Mip 0 is the largest power-of-two size not above the depth buffer's; each texel holds the farthest depth
// of the area it covers.
layout(set = PHOX_PYRAMID_SET, binding = 0) uniform sampler2D depthPyramid;

// Mirrors phox::projectSphere: the slopes x/d of the two tangents from the eye to a circle at (x, d).
vec2 tangentSlopes(float x, float d, float r) {
    float l = sqrt(x * x + d * d - r * r);
    return vec2((x * l - d * r) / (d * l + x * r), (x * l + d * r) / (d * l - x * r));
}

// Mirrors phox::projectSphere: the sphere's screen rectangle in texture coordinates, or false if it
// reaches the near plane.
bool projectSphere(vec3 center, float radius, out vec4 bounds) {
    float d = -center.z;
    float nearZ = frame.proj[3][2] / frame.proj[2][2];
    if (d - radius < nearZ) {
        return false;
    }
    vec2 x = tangentSlopes(center.x, d, radius) * frame.proj[0][0];
    vec2 y = tangentSlopes(center.y, d, radius) * frame.proj[1][1];
    bounds = vec4(min(x.x, x.y), min(y.x, y.y), max(x.x, x.y), max(y.x, y.y)) * 0.5 + 0.5;
    return true;
}

// Whether a sphere (world space) lies behind everything the early pass drew where it appears on screen.
// Conservative: the sphere's nearest point against the farthest depth over its rectangle.
bool occluded(vec3 worldCenter, float radius) {
    vec3 center = (frame.view * vec4(worldCenter, 1.0)).xyz;
    vec4 bounds;
    if (!projectSphere(center, radius, bounds)) {
        return false;
    }
    float nearest = -center.z - radius;
    float nearestDepth = (frame.proj[2][2] * -nearest + frame.proj[3][2]) / nearest;

    // The rectangle in texels of mip 0, then the mip at which it spans at most one texel: there it
    // touches at most 2 x 2 texels.
    vec2 size = vec2(textureSize(depthPyramid, 0));
    vec4 texels = clamp(bounds, 0.0, 1.0) * size.xyxy;
    float extent = max(texels.z - texels.x, texels.w - texels.y);
    int level = clamp(int(ceil(log2(max(extent, 1.0)))), 0, textureQueryLevels(depthPyramid) - 1);
    ivec2 last = textureSize(depthPyramid, level) - 1;
    float scale = 1.0 / float(1 << level);
    ivec2 lo = clamp(ivec2(floor(texels.xy * scale)), ivec2(0), last);
    ivec2 hi = clamp(ivec2(floor(texels.zw * scale)), ivec2(0), last);
    float farthest = max(max(texelFetch(depthPyramid, lo, level).r, texelFetch(depthPyramid, ivec2(hi.x, lo.y), level).r),
                         max(texelFetch(depthPyramid, ivec2(lo.x, hi.y), level).r, texelFetch(depthPyramid, hi, level).r));
    return nearestDepth > farthest;
}
