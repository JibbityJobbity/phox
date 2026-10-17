#ifndef PHOX_VULKAN_IMAGE_HPP
#define PHOX_VULKAN_IMAGE_HPP

#include "phox/Common.hpp"
#include "phox/Image.hpp"
#include "phox/vulkan/VulkanCommon.hpp"
#include "phox/vulkan/Device.hpp"

namespace phox {

namespace vk {

class Image : public phox::Image {
  private:
    Device* m_device = nullptr;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;

  public:
    Image(Device* device, VkImage image, VmaAllocation allocation);
    ~Image();
};

}

} // namespace phox

#endif // PHOX_VULKAN_IMAGE_HPP