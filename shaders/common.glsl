// Interface shared by Phox's scene shaders. Mirrors include/phox/ShaderInterface.hpp (and shaders/metal/common.h)
// and, for the meshlet path, include/phox/vulkan/ShaderInterface.hpp.
//
// Shaders of the meshlet path (forward.task, forward.mesh) define PHOX_MESHLET_DRAW before including
// this file and get MeshletDrawPushConstants; forward.vert defines PHOX_VERTEX_DRAW and gets
// DrawPushConstants. Both carry the draw's material index, which the vertex and mesh shaders pass to
// forward.frag, so one fragment shader (without push constants) serves both paths.
//
// Sets: 0 SetLayout::Frame, 1 SetLayout::Bindless (fragment shaders that define PHOX_MATERIALS), then for
// the meshlet path 2 SetLayout::MeshletVisibility and 3 SetLayout::DepthPyramid (forward_task.glsl).

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

// Set 0, binding 1: one entry per draw of the frame.
layout(set = 0, binding = 1, std430) readonly buffer Materials {
    MaterialData materials[];
};

// Set 1 (SetLayout::Bindless): every texture the renderer has uploaded, each with its own sampler. sRGB
// textures have sRGB formats, so samples come back linear.
layout(set = 1, binding = 0) uniform sampler2D textures[];

#endif
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

// phox::Vertex, read as floats: position (0-2), normal (3-5), texcoord (6-7), tangent (8-11).
const uint kVertexFloats = 12u;
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexBuffer { float values[]; };
// The meshlets, followed by phox::MeshletMesh::data (per meshlet, vertex indices, then triangles of three
// 8-bit local indices), which is read through a MeshletDataBuffer of the same address.
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletBuffer { Meshlet meshlets[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshletDataBuffer { uint values[]; };

const uint kMeshletFrustumCulling = 1u;
const uint kMeshletConeCulling = 2u;

layout(push_constant) uniform MeshletDrawPushConstants {
    mat4 model;
    vec4 cameraPosition; // xyz: the camera in the mesh's object space
    VertexBuffer vertices;
    MeshletBuffer meshlets;
    uint meshletCount;
    uint flags;            // kMeshletFrustumCulling | kMeshletConeCulling
    uint visibilityOffset; // two-phase occlusion culling: the draw's first word of meshlet visibility bits
    uint material;         // index into `materials`
} draw;

// From the task shader to the mesh shader: the meshlets that survived culling, one mesh shader
// workgroup each.
struct MeshletTask {
    uint meshlets[PHOX_TASK_GROUP_SIZE];
};

#elif defined(PHOX_VERTEX_DRAW)

// Per draw (vertex stage).
layout(push_constant) uniform DrawPushConstants {
    mat4 model;
    uint material; // index into `materials`
} draw;

#endif
