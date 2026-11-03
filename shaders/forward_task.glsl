// The task shader of the meshlet path. Culls a draw's meshlets, PHOX_TASK_GROUP_SIZE per workgroup, and
// launches one mesh shader workgroup for each it keeps. Built three ways (see the .task files):
//
//   forward.task        frustum and, for single-sided meshes, cone culling (the flags in the push constants)
//   forward_early.task  PHOX_OCCLUSION_EARLY: the same, but only for meshlets that were visible last frame
//   forward_late.task   PHOX_OCCLUSION_LATE: the same, then occlusion against the depth pyramid built from
//                       what the early pass drew. Records which meshlets are visible now, and draws those
//                       the early pass did not.
//
// Each workgroup owns one 32-bit word of visibility bits, bit i for its meshlet i, so no atomics are needed.

#extension GL_EXT_mesh_shader : require
#extension GL_EXT_buffer_reference : require
#define PHOX_MESHLET_DRAW
#include "common.glsl"

layout(local_size_x = PHOX_TASK_GROUP_SIZE) in;

taskPayloadSharedEXT MeshletTask task;

// Per invocation: bit 0, the meshlet is drawn; bit 1, it is visible (late pass).
shared uint lanes[PHOX_TASK_GROUP_SIZE];

#if defined(PHOX_OCCLUSION_EARLY) || defined(PHOX_OCCLUSION_LATE)
// Set 1 (SetLayout::MeshletVisibility): whether each meshlet was visible at the end of the previous frame.
layout(set = 1, binding = 0) buffer MeshletVisibility {
    uint words[];
} visibility;
#endif

#ifdef PHOX_OCCLUSION_LATE
// Set 2 (SetLayout::DepthPyramid): mip 0 is the largest power-of-two size not above the depth buffer's;
// each texel holds the farthest depth of the area it covers.
layout(set = 2, binding = 0) uniform sampler2D depthPyramid;

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
#endif

// Frustum and cone culling, as the flags ask. Also returns the bounding sphere in world space.
bool meshletVisible(Meshlet meshlet, out vec3 center, out float radius) {
    // Its radius grows by the largest scale of the transform.
    center = (draw.model * vec4(meshlet.sphere.xyz, 1.0)).xyz;
    float scale = sqrt(max(max(dot(draw.model[0].xyz, draw.model[0].xyz), dot(draw.model[1].xyz, draw.model[1].xyz)),
                           dot(draw.model[2].xyz, draw.model[2].xyz)));
    radius = meshlet.sphere.w * scale;
    if ((draw.flags & kMeshletFrustumCulling) != 0u) {
        for (int i = 0; i < 6; ++i) {
            if (dot(frame.frustumPlanes[i].xyz, center) + frame.frustumPlanes[i].w < -radius) {
                return false;
            }
        }
    }
    if ((draw.flags & kMeshletConeCulling) != 0u) {
        // In object space, where the cone was computed. Facing is preserved by the (non-mirroring)
        // transform, so this is exact even under non-uniform scale. Same test as phox::backFacing.
        vec3 toApex = meshlet.coneApex.xyz - draw.cameraPosition.xyz;
        float distance = length(toApex);
        if (distance > 0.0 && dot(toApex / distance, meshlet.cone.xyz) >= meshlet.cone.w) {
            return false;
        }
    }
    return true;
}

void main() {
    uint lane = gl_LocalInvocationIndex;
    uint index = gl_WorkGroupID.x * PHOX_TASK_GROUP_SIZE + lane;
#if defined(PHOX_OCCLUSION_EARLY) || defined(PHOX_OCCLUSION_LATE)
    uint word = draw.visibilityOffset + gl_WorkGroupID.x;
    bool wasVisible = (visibility.words[word] & (1u << lane)) != 0u;
#endif

    uint state = 0u;
    if (index < draw.meshletCount) {
        vec3 center;
        float radius;
        bool visible = meshletVisible(draw.meshlets.meshlets[index], center, radius);
#if defined(PHOX_OCCLUSION_EARLY)
        state = wasVisible && visible ? 1u : 0u;
#elif defined(PHOX_OCCLUSION_LATE)
        visible = visible && !occluded(center, radius);
        state = (visible && !wasVisible ? 1u : 0u) | (visible ? 2u : 0u);
#else
        state = visible ? 1u : 0u;
#endif
    }
    lanes[lane] = state;
    barrier();

    // Compact the drawn meshlets in meshlet order, so meshlets are drawn in the same order on every run.
    uint slot = 0u;
    uint count = 0u;
    uint visibleBits = 0u;
    for (uint i = 0u; i < PHOX_TASK_GROUP_SIZE; ++i) {
        uint drawn = lanes[i] & 1u;
        slot += i < lane ? drawn : 0u;
        count += drawn;
        visibleBits |= (lanes[i] >> 1) << i;
    }
    if ((state & 1u) != 0u) {
        task.meshlets[slot] = index;
    }
#ifdef PHOX_OCCLUSION_LATE
    if (lane == 0u) {
        visibility.words[word] = visibleBits;
    }
#endif
    barrier();
    EmitMeshTasksEXT(count, 1, 1);
}
