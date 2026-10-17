#include "phox/vulkan/Image.hpp"

phox::vk::Image::Image(Device* device, VkImage image, VmaAllocation allocation)
    : phox::Image(), m_device(device), m_image(image), m_allocation(allocation) {}

phox::vk::Image::~Image() {
    if (m_image == VK_NULL_HANDLE || m_allocation == nullptr ||
        m_device == nullptr || m_device->m_allocator == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyImage(m_device->m_allocator, m_image, m_allocation);
    m_image = VK_NULL_HANDLE;
    m_allocation = nullptr;
}
