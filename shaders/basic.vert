#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 viewProjection;
} camera;

void main() {
    gl_Position = camera.viewProjection * vec4(inPosition, 1.0);
}
