// Interface shared by Phox's scene shaders. Mirrors include/phox/vulkan/ShaderInterface.hpp.
//
// Shaders of the meshlet path (forward.task, forward.mesh) define PHOX_MESHLET_DRAW before including
// this file and get MeshletDrawPushConstants instead of DrawPushConstants. Both start with the same
// two members, so forward.frag serves both paths.

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

#ifdef PHOX_MESHLET_DRAW

// Needs GL_EXT_buffer_reference, enabled by the including shader.

// Meshlets per task shader workgroup: one invocation tests one meshlet.
#define PHOX_TASK_GROUP_SIZE 32

// phox::Meshlet.
struct Meshlet {
    vec4 sphere;   // xyz: centre (object space); w: radius
    vec4 cone;     // xyz: axis; w: cutoff, above 1 when there is no cone
    vec4 coneApex; // xyz
    uvec4 range;   // vertex offset, triangle offset (uints from the start of the meshlet buffer), counts
};

// phox::Vertex, read as floats: position (0-2), normal (3-5), texcoord (6-7).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexBuffer { float values[]; };
// The meshlets, followed by phox::MeshletMesh::data (per meshlet, vertex indices, then triangles of three
// 8-bit local indices), which is read through a MeshletDataBuffer of the same address.
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletBuffer { Meshlet meshlets[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletDataBuffer { uint values[]; };

const uint kMeshletFrustumCulling = 1u;
const uint kMeshletConeCulling = 2u;

layout(push_constant) uniform MeshletDrawPushConstants {
    vec4 albedo;
    mat4 model;
    vec4 cameraPosition; // xyz: the camera in the mesh's object space
    VertexBuffer vertices;
    MeshletBuffer meshlets;
    uint meshletCount;
    uint flags; // kMeshletFrustumCulling | kMeshletConeCulling
} draw;

// From the task shader to the mesh shader: the meshlets that survived culling, one mesh shader
// workgroup each.
struct MeshletTask {
    uint meshlets[PHOX_TASK_GROUP_SIZE];
};

#else

// Per draw. albedo comes first: llvmpipe (seen in Mesa 25.1) has a fast path for fragment shaders that
// output a constant, and it reads that constant from offset 0 whatever the shader's offset is.
layout(push_constant) uniform DrawPushConstants {
    vec4 albedo;
    mat4 model;
} draw;

#endif
