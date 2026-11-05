// Interface shared by Phox's scene shaders. Mirrors include/phox/ShaderInterface.hpp (and shaders/metal/common.h).

// Set 0 (SetLayout::Frame): per-frame data.
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPosition; // xyz
    vec4 lightDirection; // xyz: unit vector towards the light; w: ambient term
    vec4 lightColor;     // rgb
    vec4 resolution;     // xy: target size in pixels; zw: 1 / size
} frame;

// Per draw. albedo comes first: llvmpipe (seen in Mesa 25.1) has a fast path for fragment shaders that
// output a constant, and it reads that constant from offset 0 whatever the shader's offset is.
layout(push_constant) uniform DrawPushConstants {
    vec4 albedo;
    mat4 model;
} draw;
