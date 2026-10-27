#ifndef PHOX_VULKAN_IMAGE_HPP
#define PHOX_VULKAN_IMAGE_HPP

#include "phox/Image.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox::vk {

class Device;

/// An image, its memory and a view of all of its mips and layers.
class Image final : public phox::Image {
public:
    Image(Device &device, const ImageCreateInfo &info, VkImage image, VmaAllocation allocation, VkImageView view);
    ~Image() override;

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }

private:
    Device &m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
    VkImageView m_view = VK_NULL_HANDLE;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_IMAGE_HPP
