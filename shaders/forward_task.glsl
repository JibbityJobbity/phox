// The task shader of the meshlet path. Each workgroup runs one work item that the instance cull pass
// (cull.glsl) appended: PHOX_TASK_GROUP_SIZE meshlets of one drawn instance. It culls them and launches
// one mesh shader workgroup for each it keeps. A mesh's meshlets are every level of its cluster LOD
// hierarchy (phox::buildMeshletLod); first each picks whether its level is the one to draw here
// (lodSelected), then the culling tests below. Built three ways (see the .task files):
//
//   forward.task        frustum and, for single-sided meshes, cone culling (the flags in the push constants)
//   forward_early.task  PHOX_OCCLUSION_EARLY: the same, but only for meshlets that were visible last frame
//   forward_late.task   PHOX_OCCLUSION_LATE: the same, then occlusion against the depth pyramid built from
//                       what the early pass drew. Records which meshlets are visible now, and draws those
//                       the early pass did not.
//
// Each work item owns one 32-bit word of visibility bits, bit i for its meshlet i, so no atomics are needed.

#extension GL_EXT_mesh_shader : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#define PHOX_MESHLET_DRAW
#include "common.glsl"

#ifdef PHOX_OCCLUSION_LATE
#define PHOX_PYRAMID_SET 2
#include "occlusion.glsl"
#endif

layout(local_size_x = PHOX_TASK_GROUP_SIZE) in;

taskPayloadSharedEXT MeshletTask task;

// Per invocation: bit 0, the meshlet is drawn; bit 1, it is visible (late pass).
shared uint lanes[PHOX_TASK_GROUP_SIZE];

// Mirrors phox::lodProjectedError: @p error at @p sphere (object space) as seen from @p camera (object
// space), in pixels. `precise`: one group's sphere is tested as its meshlets' own and as the parent of the
// meshlets it was made from, which must reach the same answer bit for bit. Without it the compiler may fuse
// the two evaluations differently, and a group right at the threshold is then drawn at both levels or neither.
float lodProjectedError(vec4 sphere, float error, vec3 camera) {
    if (error >= kLodNoParent) {
        return kLodNoParent;
    }
    precise vec3 offset = sphere.xyz - camera;
    precise float distance = max(sqrt(dot(offset, offset)) - sphere.w, 1e-6);
    precise float pixelsPerUnit = abs(frame.proj[1][1]) * 0.5 * frame.resolution.y;
    precise float projected = error / distance * pixelsPerUnit;
    return projected;
}

// Mirrors phox::lodSelected: this meshlet, and not a finer or coarser version of it, is drawn. Every
// meshlet a group made shares that group's sphere and error, so they all decide alike: the drawn
// meshlets cover the mesh once, without cracks.
bool lodSelected(Meshlet meshlet, vec3 camera) {
    return lodProjectedError(meshlet.lodSphere, meshlet.lodError, camera) <= pass.lodThreshold &&
           lodProjectedError(meshlet.parentSphere, meshlet.parentError, camera) > pass.lodThreshold;
}

// Frustum and cone culling, as the flags ask. Also returns the bounding sphere in world space.
bool meshletVisible(Meshlet meshlet, GpuDraw draw, out vec3 center, out float radius) {
    // Its radius grows by the largest scale of the transform.
    center = (draw.model * vec4(meshlet.sphere.xyz, 1.0)).xyz;
    float scale = sqrt(max(max(dot(draw.model[0].xyz, draw.model[0].xyz), dot(draw.model[1].xyz, draw.model[1].xyz)),
                           dot(draw.model[2].xyz, draw.model[2].xyz)));
    radius = meshlet.sphere.w * scale;
    if ((pass.flags & kMeshletFrustumCulling) != 0u) {
        for (int i = 0; i < 6; ++i) {
            if (dot(frame.frustumPlanes[i].xyz, center) + frame.frustumPlanes[i].w < -radius) {
                return false;
            }
        }
    }
    if ((pass.flags & kMeshletConeCulling) != 0u && (draw.flags & kDrawConeCulling) != 0u) {
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
    // The dispatch is 2D when there are more items than one dimension takes, so some groups have none.
    uint group = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    if (group >= CounterBuffer(pass.counters).workCount[pass.bucket]) {
        EmitMeshTasksEXT(0, 1, 1);
    }
    uvec2 item = WorkBuffer(pass.work).items[pass.bucket * pass.workCapacity + group];
    GpuDraw draw = DrawBuffer(pass.draws).draws[item.x];
    uint lane = gl_LocalInvocationIndex;
    uint index = item.y * PHOX_TASK_GROUP_SIZE + lane;
#if defined(PHOX_OCCLUSION_EARLY) || defined(PHOX_OCCLUSION_LATE)
    VisibilityBuffer visibility = VisibilityBuffer(pass.visibility);
    uint word = draw.visibilityOffset + item.y;
    // Only instances the early pass drew have meaningful bits: one that was hidden last frame, or whose
    // slot was just reused, gets all its meshlets considered in the late pass.
    bool wasVisible = (draw.flags & kDrawEarly) != 0u && (visibility.words[word] & (1u << lane)) != 0u;
#endif

    uint state = 0u;
    if (index < draw.count) {
        vec3 center;
        float radius;
        Meshlet meshlet = MeshletBuffer(draw.primitives).meshlets[index];
        // A meshlet of the wrong level is no part of the picture at all: not drawn, and not visible.
        bool visible = lodSelected(meshlet, draw.cameraPosition.xyz) && meshletVisible(meshlet, draw, center, radius);
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
    task.draw = item.x;
#ifdef PHOX_OCCLUSION_LATE
    if (lane == 0u) {
        visibility.words[word] = visibleBits;
    }
#endif
    barrier();
    EmitMeshTasksEXT(count, 1, 1);
}
