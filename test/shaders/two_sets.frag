#version 450
layout(location = 0) out vec4 outColor;

// Set 1 on purpose: a pipeline with two descriptor sets cannot use push descriptors.
layout(set = 1, binding = 0) uniform Material {
    vec4 albedo;
} material;

void main() {
    outColor = material.albedo;
}
