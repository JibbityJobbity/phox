// Interface shared by Phox's Metal scene shaders: the MSL counterpart of shaders/common.glsl. The data
// blocks mirror include/phox/ShaderInterface.hpp and the buffer indices include/phox/metal/ShaderInterface.hpp.
//
// The Metal ShaderLibrary compiles these files at runtime and inlines `#include "common.h"` itself.
#include <metal_stdlib>
using namespace metal;

// Buffer and texture indices (the Metal counterpart of descriptor sets and push constants).
#define PHOX_FRAME_BUFFER 0   // FrameUniforms (SetLayout::Frame)
#define PHOX_DRAW_BUFFER 1    // per-draw constants (the Vulkan push constants)
#define PHOX_VERTEX_BUFFER 30 // vertex data, described by the pipeline's vertex descriptor
#define PHOX_TEXTURE 0        // SetLayout::Texture

struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4 cameraPosition; // xyz
    float4 lightDirection; // xyz: unit vector towards the light; w: ambient term
    float4 lightColor;     // rgb
    float4 resolution;     // xy: target size in pixels; zw: 1 / size
};

struct DrawPushConstants {
    float4 albedo;
    float4x4 model;
};

// Phox's projection targets Vulkan clip space, whose +y points down the screen; Metal's points up.
// Flipping y here puts every vertex on the same pixel as in Vulkan. Window-space winding is then the
// same too, so the pipelines keep Vulkan's front-face convention.
inline float4 toMetalClip(float4 position) {
    return float4(position.x, -position.y, position.z, position.w);
}
