#ifndef PHOX_METAL_SHADER_INTERFACE_HPP
#define PHOX_METAL_SHADER_INTERFACE_HPP

#include "phox/ShaderInterface.hpp"

#include <cstdint>

/// Where the Metal shaders (shaders/metal/common.h) expect their data. Metal has no descriptor sets or
/// push constants: each SetLayout and the per-draw constants get a fixed buffer or texture index.
namespace phox::mtl {

using phox::DrawPushConstants;
using phox::FrameUniforms;
using phox::TonemapPushConstants;

inline constexpr std::uint32_t kFrameBufferIndex = 0;   ///< FrameUniforms (SetLayout::Frame), vertex and fragment
inline constexpr std::uint32_t kDrawBufferIndex = 1;    ///< per-draw constants, where Vulkan uses push constants
inline constexpr std::uint32_t kVertexBufferIndex = 30; ///< vertex data (the pipeline's vertex descriptor)
inline constexpr std::uint32_t kTextureIndex = 0;       ///< SetLayout::Texture, fragment

} // namespace phox::mtl

#endif // PHOX_METAL_SHADER_INTERFACE_HPP
