#ifndef PHOX_VULKAN_SHADER_INTERFACE_HPP
#define PHOX_VULKAN_SHADER_INTERFACE_HPP

#include "phox/ShaderInterface.hpp"

/// The shader data blocks, shared by every backend (phox/ShaderInterface.hpp). In GLSL they are declared
/// in shaders/common.glsl: FrameUniforms is set 0, binding 0 (SetLayout::Frame), DrawPushConstants the
/// push constant block.
namespace phox::vk {

using phox::DrawPushConstants;
using phox::FrameUniforms;
using phox::TonemapPushConstants;

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_INTERFACE_HPP
