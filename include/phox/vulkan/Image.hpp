#ifndef PHOX_VULKAN_IMAGE_HPP
#define PHOX_VULKAN_IMAGE_HPP

#include "phox/Image.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <vector>

namespace phox::vk {

class Device;

/// An image, its memory and a view of all of its mips and layers, plus views of single mips on request.
class Image final : public phox::Image {
public:
    Image(Device &device, const ImageCreateInfo &info, VkImage image, VmaAllocation allocation, VkImageView view);
    ~Image() override;

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }
    /// A view of mip @p level alone (all layers), e.g. to bind it as a storage image. Created on first use
    /// and kept as long as the image.
    /// @throws std::out_of_range if the image has no such mip
    VkImageView mipView(std::uint32_t level);

private:
    Device &m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
    VkImageView m_view = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> m_mipViews; ///< by level; VK_NULL_HANDLE until requested
};

} // namespace phox::vk

#endif // PHOX_VULKAN_IMAGE_HPP
