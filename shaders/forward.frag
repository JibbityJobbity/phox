#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(inNormal);
    if (!gl_FrontFacing) {
        normal = -normal; // light single-sided geometry from both sides
    }
    float ambient = frame.lightDirection.w;
    float diffuse = max(dot(normal, frame.lightDirection.xyz), 0.0);
    vec3 color = draw.albedo.rgb * frame.lightColor.rgb * (ambient + (1.0 - ambient) * diffuse);
    outColor = vec4(color, draw.albedo.a);
}
