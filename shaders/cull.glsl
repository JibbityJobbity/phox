// The instance cull pass: the first step of the GPU-driven forward pass. One invocation per instance slot
// tests the object against the view frustum and, with occlusion culling, against the depth pyramid, and
// for each it keeps appends a GpuDraw and its work: a work item per PHOX_TASK_GROUP_SIZE meshlets for the
// task shader (meshlet path) or an indexed indirect draw (vertex path, PHOX_CULL_VERTEX). Work goes to
// bucket 1 for single-sided instances (drawn by the pipeline that culls back faces), else bucket 0.
// Built four ways (see the .comp files):
//
//   cull.comp         frustum culling, then every surviving instance's meshlets
//   cull_early.comp   PHOX_OCCLUSION_EARLY: only instances that were visible at the end of the previous
//                     frame (the history), drawn by the early pass from their visible meshlets
//   cull_late.comp    PHOX_OCCLUSION_LATE: every instance in the frustum that the early pass drew, or that
//                     is not hidden behind its depth; rewrites the history
//   cull_vertex.comp  PHOX_CULL_VERTEX: frustum culling, then an indexed draw per instance
//
// Appending uses atomics, so the order of draws differs between runs; depth testing makes the picture the
// same whatever the order.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#define PHOX_CULL_PASS
#include "common.glsl"

#ifdef PHOX_OCCLUSION_LATE
#define PHOX_PYRAMID_SET 1
#include "occlusion.glsl"
#endif

layout(local_size_x = 64) in;

void main() {
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= pass.instanceCount) {
        return;
    }
    GpuInstance instance = InstanceBuffer(pass.instances).instances[slot];
#if defined(PHOX_OCCLUSION_EARLY) || defined(PHOX_OCCLUSION_LATE)
    HistoryBuffer history = HistoryBuffer(pass.history);
#endif
    if (instance.mesh == kNoMesh) {
#ifdef PHOX_OCCLUSION_LATE
        history.visible[slot] = 0u; // a later occupant of the slot starts unseen
#endif
        return;
    }
    GpuMesh mesh = MeshBuffer(pass.meshes).meshes[instance.mesh];

    // The mesh's sphere in world space; its radius grows by the largest scale of the transform.
    vec3 center = (instance.model * vec4(mesh.sphere.xyz, 1.0)).xyz;
    float scale = sqrt(max(max(dot(instance.model[0].xyz, instance.model[0].xyz),
                               dot(instance.model[1].xyz, instance.model[1].xyz)),
                           dot(instance.model[2].xyz, instance.model[2].xyz)));
    float radius = mesh.sphere.w * scale;
    bool visible = true;
    if ((pass.flags & kCullFrustum) != 0u) {
        for (int i = 0; i < 6; ++i) {
            if (dot(frame.frustumPlanes[i].xyz, center) + frame.frustumPlanes[i].w < -radius) {
                visible = false;
            }
        }
    }
#if defined(PHOX_OCCLUSION_EARLY)
    bool early = true;
    visible = visible && history.visible[slot] != 0u;
#elif defined(PHOX_OCCLUSION_LATE)
    // The same test as the early pass: these instances were drawn there, from last frame's visible
    // meshlets, and their other meshlets still need testing.
    bool early = visible && history.visible[slot] != 0u;
    visible = visible && (early || !occluded(center, radius));
    history.visible[slot] = visible ? 1u : 0u;
#else
    bool early = false;
#endif
    if (!visible) {
        return;
    }

    CounterBuffer counters = CounterBuffer(pass.counters);
    uint drawIndex = atomicAdd(counters.drawCount, 1u);
    GpuDraw draw;
    draw.model = instance.model;
    draw.cameraPosition = inverse(instance.model) * vec4(frame.cameraPosition.xyz, 1.0);
    draw.vertices = mesh.vertices;
#ifdef PHOX_CULL_VERTEX
    draw.primitives = uvec2(0u);
    draw.count = mesh.indexCount;
#else
    draw.primitives = mesh.meshlets;
    draw.count = mesh.meshletCount;
#endif
    draw.material = slot;
    draw.visibilityOffset = instance.visibilityOffset;
    bool cullBackFaces = (instance.flags & kInstanceCullBackFaces) != 0u;
    draw.flags = (cullBackFaces ? kDrawConeCulling : 0u) | (early ? kDrawEarly : 0u);
    DrawBuffer(pass.draws).draws[drawIndex] = draw;

    uint bucket = cullBackFaces ? 1u : 0u;
#ifdef PHOX_CULL_VERTEX
    uint command = bucket * pass.workCapacity + atomicAdd(counters.workCount[bucket], 1u);
    // Field by field: glslang gives a whole-struct store through a buffer reference an invalid alignment.
    CommandBuffer commands = CommandBuffer(pass.work);
    commands.commands[command].indexCount = mesh.indexCount;
    commands.commands[command].instanceCount = 1u;
    commands.commands[command].firstIndex = mesh.firstIndex;
    commands.commands[command].vertexOffset = 0;
    commands.commands[command].firstInstance = drawIndex;
#else
    uint groups = (mesh.meshletCount + PHOX_TASK_GROUP_SIZE - 1u) / PHOX_TASK_GROUP_SIZE;
    uint first = atomicAdd(counters.workCount[bucket], groups);
    WorkBuffer work = WorkBuffer(pass.work);
    for (uint g = 0u; g < groups; ++g) {
        work.items[bucket * pass.workCapacity + first + g] = uvec2(drawIndex, g);
    }
#endif
}
