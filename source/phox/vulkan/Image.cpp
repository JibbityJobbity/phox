#include "phox/vulkan/Image.hpp"

#include "phox/vulkan/Device.hpp"

namespace phox::vk {

Image::Image(Device &device, const ImageCreateInfo &info, VkImage image, VmaAllocation allocation, VkImageView view)
    : phox::Image(info), m_device(device), m_image(image), m_allocation(allocation), m_view(view) {}

Image::~Image() {
    m_device.table().vkDestroyImageView(m_device.handle(), m_view, nullptr);
    vmaDestroyImage(m_device.allocator(), m_image, m_allocation);
}

} // namespace phox::vk
