#ifndef PHOX_VULKAN_CONVERSIONS_HPP
#define PHOX_VULKAN_CONVERSIONS_HPP

#include "phox/Resources.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox::vk {

VkFormat toVkFormat(Format format);
/// Format::Undefined for formats Phox has no name for.
Format fromVkFormat(VkFormat format);
VkImageAspectFlags aspectMask(Format format);
/// Creation flags covering every usage in @p usage.
VkImageUsageFlags toVkImageUsage(ImageUsageMask usage);
VkBufferUsageFlags toVkBufferUsage(BufferUsageMask usage);

} // namespace phox::vk

#endif // PHOX_VULKAN_CONVERSIONS_HPP
