#include "phox/vulkan/Image.hpp"

#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Device.hpp"

#include <stdexcept>
#include <string>

namespace phox::vk {

Image::Image(Device &device, const ImageCreateInfo &info, VkImage image, VmaAllocation allocation, VkImageView view)
    : phox::Image(info), m_device(device), m_image(image), m_allocation(allocation), m_view(view),
      m_format(toVkFormat(info.format)), m_mipViews(info.mipLevels, VK_NULL_HANDLE) {}

Image::~Image() {
    for (VkImageView mipView : m_mipViews) m_device.table().vkDestroyImageView(m_device.handle(), mipView, nullptr);
    m_device.table().vkDestroyImageView(m_device.handle(), m_view, nullptr);
    vmaDestroyImage(m_device.allocator(), m_image, m_allocation);
}

VkImageView Image::mipView(std::uint32_t level) {
    VkImageView &view = m_mipViews.at(level);
    if (view != VK_NULL_HANDLE) {
        return view;
    }
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = m_image;
    info.viewType = this->info().arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    info.format = m_format;
    info.subresourceRange = {aspectMask(this->info().format), level, 1, 0, this->info().arrayLayers};
    if (m_device.table().vkCreateImageView(m_device.handle(), &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create a view of mip " + std::to_string(level) + " of image '" +
                                 this->info().name + "'");
    }
    m_device.setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view),
                           this->info().name + " mip " + std::to_string(level));
    return view;
}

} // namespace phox::vk
