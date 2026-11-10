// Interface shared by Phox's scene shaders. Mirrors include/phox/ShaderInterface.hpp (and shaders/metal/common.h)
// and, for the meshlet path, include/phox/vulkan/ShaderInterface.hpp.
//
// Each shader defines which push constants it gets before including this file: PHOX_MESHLET_DRAW (the
// task and mesh shaders), PHOX_VERTEX_DRAW (forward.vert), PHOX_CULL_PASS (cull.glsl), PHOX_ARGS_PASS
// (draw_args.comp) or PHOX_SCENE_UPDATE_PASS (scene_update.comp). The vertex and mesh shaders pass the
// draw's material index to forward.frag, so one fragment shader (without push constants) serves both paths.
//
// Sets: 0 SetLayout::Frame, 1 SetLayout::Bindless (fragment shaders that define PHOX_MATERIALS), then for
// the late occlusion passes 2 (the task shader) or 1 (the cull pass) SetLayout::DepthPyramid.

// Set 0 (SetLayout::Frame): per-frame data.
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPosition; // xyz
    vec4 lightDirection; // xyz: unit vector towards the light; w: ambient term
    vec4 lightColor;     // rgb
    vec4 resolution;     // xy: target size in pixels; zw: 1 / size
    vec4 frustumPlanes[6]; // world space; xyz: unit normal pointing inwards, w: distance
} frame;

#ifdef PHOX_MATERIALS

// Needs GL_EXT_nonuniform_qualifier, enabled by the including shader.

// phox::MaterialData. Texture fields index `textures`, or are kNoTexture.
struct MaterialData {
    vec4 baseColor;
    vec4 emissive; // rgb
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint flags; // kMaterialAlphaMask | kMaterialAlphaBlend
    uint baseColorTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
    uint padding;
};

const uint kNoTexture = 0xFFFFFFFFu;
const uint kMaterialAlphaMask = 1u;
const uint kMaterialAlphaBlend = 2u;

// Set 0, binding 1: the scene's materials, one entry per instance slot (GpuScene), kept across frames.
layout(set = 0, binding = 1, std430) readonly buffer Materials {
    MaterialData materials[];
};

// Set 1 (SetLayout::Bindless): every texture the renderer has uploaded, each with its own sampler. sRGB
// textures have sRGB formats, so samples come back linear.
layout(set = 1, binding = 0) uniform sampler2D textures[];

#endif
#if defined(PHOX_MESHLET_DRAW) || defined(PHOX_VERTEX_DRAW) || defined(PHOX_CULL_PASS) || defined(PHOX_ARGS_PASS) || \
    defined(PHOX_SCENE_UPDATE_PASS)

// The GPU-driven scene (include/phox/vulkan/ShaderInterface.hpp). Buffers are reached through device
// addresses kept as uvec2, turned into references with GL_EXT_buffer_reference_uvec2: needs that and
// GL_EXT_buffer_reference, enabled by the including shader.

// Meshlets per task shader workgroup, and so per work item: one invocation tests one meshlet.
#define PHOX_TASK_GROUP_SIZE 32

// phox::Meshlet.
struct Meshlet {
    vec4 sphere;       // xyz: centre (object space); w: radius
    vec4 cone;         // xyz: axis; w: cutoff, above 1 when there is no cone
    vec4 coneApex;     // xyz
    vec4 lodSphere;    // where lodError is measured
    vec4 parentSphere; // where parentError is measured
    float lodError;    // object space; 0 for the original triangles
    float parentError; // kLodNoParent: no coarser version
    uint level;
    uint padding;
    uvec4 range; // vertex offset, triangle offset (uints from the start of the meshlet buffer), counts
};

const float kLodNoParent = 3.0e38;

// One scene object, an entry of the instance array (index: the object's slot), kept on the GPU across
// frames. phox::vk::GpuInstance.
struct GpuInstance {
    mat4 model;
    uint mesh;             // index into the mesh array; kNoMesh: an empty slot, or drawn by the CPU (blended)
    uint flags;            // kInstanceCullBackFaces
    uint visibilityOffset; // first word of the slot's meshlet visibility bits
    uint padding;
};

// One mesh of the frame. phox::vk::GpuMesh.
struct GpuMesh {
    vec4 sphere;       // object space: xyz centre, w radius, enclosing the mesh
    uvec2 vertices;    // address of its phox::Vertex array
    uvec2 meshlets;    // meshlet path: its Meshlet array, followed by their data
    uint meshletCount; // of every LOD level
    uint indexCount;   // vertex path
    uint firstIndex;   // vertex path: where its indices start in the index pool (bound as the index buffer)
    uint padding;
};

// One instance the cull pass decided to draw. phox::vk::GpuDraw.
struct GpuDraw {
    mat4 model;
    vec4 cameraPosition;   // xyz: the camera in the instance's object space
    uvec2 vertices;
    uvec2 primitives;      // meshlet path: GpuMesh::meshlets; unused on the vertex path
    uint count;            // meshlets, or indices
    uint material;         // index into `materials` (the object's slot)
    uint visibilityOffset; // first word of meshlet visibility bits
    uint flags;            // kDrawConeCulling | kDrawEarly
};

const uint kNoMesh = 0xFFFFFFFFu;
const uint kInstanceCullBackFaces = 1u;
const uint kDrawConeCulling = 1u; // single-sided: meshlets facing away may be skipped
const uint kDrawEarly = 2u;       // occlusion culling: the early pass drew this instance's visible meshlets

// phox::Vertex, read as floats: position (0-2), normal (3-5), texcoord (6-7), tangent (8-11).
const uint kVertexFloats = 12u;
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexBuffer { float values[]; };
// The meshlets, followed by phox::MeshletMesh::data (per meshlet, vertex indices, then triangles of three
// 8-bit local indices), which is read through a MeshletDataBuffer of the same address.
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletBuffer { Meshlet meshlets[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletDataBuffer { uint values[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer InstanceBuffer { GpuInstance instances[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshBuffer { GpuMesh meshes[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) buffer DrawBuffer { GpuDraw draws[]; };
// Meshlet path: (draw, group of PHOX_TASK_GROUP_SIZE meshlets) per task shader workgroup.
layout(buffer_reference, std430, buffer_reference_align = 8) buffer WorkBuffer { uvec2 items[]; };
// Vertex path: a VkDrawIndexedIndirectCommand per draw, firstInstance = the draw.
struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(buffer_reference, std430, buffer_reference_align = 4) buffer CommandBuffer { DrawCommand commands[]; };
// phox::vk::DrawCounters: what one cull pass produced, per bucket (0: both faces drawn, 1: back faces
// culled), and the indirect arguments the args pass makes of it.
layout(buffer_reference, std430, buffer_reference_align = 16) buffer CounterBuffer {
    uint drawCount;
    uint workCount[2]; // work items (meshlet path) or DrawCommands (vertex path)
    uint padding;
    uvec4 args[2];     // VkDrawMeshTasksIndirectCommandEXT per bucket (xyz)
};
layout(buffer_reference, std430, buffer_reference_align = 4) buffer VisibilityBuffer { uint words[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer HistoryBuffer { uint visible[]; };

#endif

#ifdef PHOX_MESHLET_DRAW

// Per indirect draw (task and mesh stages): which list of work items to run.
layout(push_constant) uniform MeshletDrawPushConstants {
    uvec2 draws;      // DrawBuffer
    uvec2 work;       // WorkBuffer: bucket b's items start at b * workCapacity
    uvec2 counters;   // CounterBuffer
    uvec2 visibility; // VisibilityBuffer (occlusion culling)
    uint bucket;
    uint workCapacity;
    uint flags;         // kMeshletFrustumCulling | kMeshletConeCulling
    float lodThreshold; // cluster LOD: the error, in pixels, a meshlet may show
} pass;

const uint kMeshletFrustumCulling = 1u;
const uint kMeshletConeCulling = 2u;

// From the task shader to the mesh shader: the draw, and the meshlets that survived culling, one mesh
// shader workgroup each.
struct MeshletTask {
    uint draw;
    uint meshlets[PHOX_TASK_GROUP_SIZE];
};

#elif defined(PHOX_VERTEX_DRAW)

// Per indirect draw (vertex stage). gl_InstanceIndex is the draw (VkDrawIndirectCommand::firstInstance).
layout(push_constant) uniform VertexDrawPushConstants {
    uvec2 draws; // DrawBuffer
} pass;

#elif defined(PHOX_CULL_PASS)

// The instance cull pass (cull.comp).
layout(push_constant) uniform CullPushConstants {
    uvec2 instances; // InstanceBuffer
    uvec2 meshes;    // MeshBuffer
    uvec2 draws;     // DrawBuffer, appended to
    uvec2 work;      // WorkBuffer (meshlet path) or CommandBuffer (vertex path), appended to per bucket
    uvec2 counters;  // CounterBuffer
    uvec2 history;   // HistoryBuffer: whether each slot was visible at the end of the previous frame
    uint instanceCount;
    uint workCapacity; // entries per bucket
    uint flags;        // kCullFrustum
    uint padding;
} pass;

const uint kCullFrustum = 1u;

#elif defined(PHOX_ARGS_PASS)

layout(push_constant) uniform ArgsPushConstants {
    uvec2 counters;     // CounterBuffer
    uint maxGroupsX;    // the device's largest task shader dispatch in x
    uint padding;
} pass;

#elif defined(PHOX_SCENE_UPDATE_PASS)

// The scene update pass (scene_update.comp): changed slots' records, scattered into the scene's arrays.
struct InstanceUpdate { // phox::vk::InstanceUpdate
    GpuInstance instance;
    uint slot;
    uint padding0, padding1, padding2;
};
struct MaterialUpdate { // phox::vk::MaterialUpdate; the MaterialData as raw words
    uvec4 material[5];
    uint slot;
    uint padding0, padding1, padding2;
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer InstanceUpdateBuffer { InstanceUpdate items[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MaterialUpdateBuffer { MaterialUpdate items[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) writeonly buffer InstanceTarget { GpuInstance instances[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) writeonly buffer MaterialTarget { uvec4 words[]; };

layout(push_constant) uniform SceneUpdatePushConstants {
    uvec2 instanceUpdates; // InstanceUpdateBuffer
    uvec2 materialUpdates; // MaterialUpdateBuffer
    uvec2 instances;       // InstanceTarget: the scene's GpuInstance array
    uvec2 materials;       // MaterialTarget: the scene's MaterialData array, five words each
    uint instanceUpdateCount;
    uint materialUpdateCount;
} pass;

#endif
