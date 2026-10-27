#include "phox/vulkan/Conversions.hpp"

#include <stdexcept>

namespace phox::vk {

VkFormat toVkFormat(Format format) {
    switch (format) {
    case Format::Undefined: return VK_FORMAT_UNDEFINED;
    case Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
    case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
    case Format::D24UnormS8: return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    throw std::logic_error("unknown Format");
}

Format fromVkFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8Unorm;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8Srgb;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8Srgb;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16Float;
    case VK_FORMAT_R32_SFLOAT: return Format::R32Float;
    case VK_FORMAT_D32_SFLOAT: return Format::D32Float;
    case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24UnormS8;
    default: return Format::Undefined;
    }
}

VkImageAspectFlags aspectMask(Format format) {
    if (!isDepthFormat(format)) {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    return hasStencil(format) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
}

VkImageUsageFlags toVkImageUsage(ImageUsageMask usage) {
    const auto has = [usage](ImageUsage u) { return (usage & usageBit(u)) != 0; };
    VkImageUsageFlags flags = 0;
    if (has(ImageUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (has(ImageUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (has(ImageUsage::DepthReadOnly)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (has(ImageUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (has(ImageUsage::StorageRead) || has(ImageUsage::StorageWrite)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (has(ImageUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (has(ImageUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkBufferUsageFlags toVkBufferUsage(BufferUsageMask usage) {
    const auto has = [usage](BufferUsage u) { return (usage & usageBit(u)) != 0; };
    VkBufferUsageFlags flags = 0;
    if (has(BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (has(BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (has(BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has(BufferUsage::StorageRead) || has(BufferUsage::StorageWrite)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (has(BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (has(BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (has(BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

} // namespace phox::vk
