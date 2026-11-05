// Interface shared by Phox's Metal scene shaders: the MSL counterpart of shaders/common.glsl. The data
// blocks mirror include/phox/ShaderInterface.hpp and the buffer indices include/phox/metal/ShaderInterface.hpp.
//
// The Metal ShaderLibrary compiles these files at runtime and inlines `#include "common.h"` itself.
#include <metal_stdlib>
using namespace metal;

// Buffer and texture indices (the Metal counterpart of descriptor sets and push constants).
#define PHOX_FRAME_BUFFER 0    // FrameUniforms (SetLayout::Frame)
#define PHOX_DRAW_BUFFER 1     // per-draw constants (the Vulkan push constants)
#define PHOX_MATERIAL_BUFFER 2 // the draw's MaterialData, fragment stage
#define PHOX_VERTEX_BUFFER 30  // vertex data, described by the pipeline's vertex descriptor
#define PHOX_TEXTURE 0         // SetLayout::Texture
// A material's textures and their samplers, fragment stage. Where Vulkan indexes one texture table,
// Metal binds each draw's five (a white stand-in for those it does not have).
#define PHOX_BASE_COLOR_TEXTURE 0
#define PHOX_METALLIC_ROUGHNESS_TEXTURE 1
#define PHOX_NORMAL_TEXTURE 2
#define PHOX_OCCLUSION_TEXTURE 3
#define PHOX_EMISSIVE_TEXTURE 4

struct FrameUniforms {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4 cameraPosition; // xyz
    float4 lightDirection; // xyz: unit vector towards the light; w: ambient term
    float4 lightColor;     // rgb
    float4 resolution;     // xy: target size in pixels; zw: 1 / size
    float4 frustumPlanes[6]; // world space; the Metal backend has no meshlet path and does not read them
};

struct DrawPushConstants {
    float4x4 model;
    uint material; // unused on Metal: the draw's material is bound at PHOX_MATERIAL_BUFFER
    uint padding[3];
};

// phox::MaterialData. A texture field is kNoTexture where the material has no such texture.
struct MaterialData {
    float4 baseColor;
    float4 emissive; // rgb
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

constant uint kNoTexture = 0xFFFFFFFFu;
constant uint kMaterialAlphaMask = 1u;
constant uint kMaterialAlphaBlend = 2u;

// Phox's projection targets Vulkan clip space, whose +y points down the screen; Metal's points up.
// Flipping y here puts every vertex on the same pixel as in Vulkan. Window-space winding is then the
// same too, so the pipelines keep Vulkan's front-face convention.
inline float4 toMetalClip(float4 position) {
    return float4(position.x, -position.y, position.z, position.w);
}
