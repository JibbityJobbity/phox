#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#define PHOX_VERTEX_DRAW
#include "common.glsl"

// The vertex path of the GPU-driven forward pass: one indexed indirect draw per drawn instance (the cull
// pass writes them) from the index pool every mesh shares, firstInstance = the instance's GpuDraw. The
// shader fetches its vertex itself, so one pipeline draws every mesh without rebinding vertex buffers.

// Keep in step with forward.mesh, which produces the same outputs.
layout(location = 0) out vec3 outPosition; // world space
layout(location = 1) out vec3 outNormal;   // world space
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outTangent;  // world space xyz, handedness w
layout(location = 4) flat out uint outMaterial;

void main() {
    GpuDraw draw = DrawBuffer(pass.draws).draws[gl_InstanceIndex];
    uint base = uint(gl_VertexIndex) * kVertexFloats; // the mesh's own index, from the pool
    VertexBuffer vertices = VertexBuffer(draw.vertices);
    vec3 position = vec3(vertices.values[base], vertices.values[base + 1u], vertices.values[base + 2u]);
    vec3 normal = vec3(vertices.values[base + 3u], vertices.values[base + 4u], vertices.values[base + 5u]);
    vec2 texCoord = vec2(vertices.values[base + 6u], vertices.values[base + 7u]);
    vec4 tangent = vec4(vertices.values[base + 8u], vertices.values[base + 9u], vertices.values[base + 10u],
                        vertices.values[base + 11u]);

    vec4 world = draw.model * vec4(position, 1.0);
    outPosition = world.xyz;
    // Inverse-transpose keeps normals perpendicular under non-uniform scale; tangents lie in the surface,
    // so they transform like positions.
    outNormal = mat3(transpose(inverse(draw.model))) * normal;
    outTexCoord = texCoord;
    outTangent = vec4(mat3(draw.model) * tangent.xyz, tangent.w);
    outMaterial = draw.material;
    gl_Position = frame.viewProj * world;
}
