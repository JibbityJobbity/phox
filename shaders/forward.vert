#version 450
#extension GL_GOOGLE_include_directive : require
#define PHOX_VERTEX_DRAW
#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Keep in step with forward.mesh, which produces the same outputs.
layout(location = 0) out vec3 outPosition; // world space
layout(location = 1) out vec3 outNormal;   // world space
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outTangent;  // world space xyz, handedness w
layout(location = 4) flat out uint outMaterial;

void main() {
    vec4 world = draw.model * vec4(inPosition, 1.0);
    outPosition = world.xyz;
    // Inverse-transpose keeps normals perpendicular under non-uniform scale; tangents lie in the surface,
    // so they transform like positions.
    outNormal = mat3(transpose(inverse(draw.model))) * inNormal;
    outTexCoord = inTexCoord;
    outTangent = vec4(mat3(draw.model) * inTangent.xyz, inTangent.w);
    outMaterial = draw.material;
    gl_Position = frame.viewProj * world;
}
