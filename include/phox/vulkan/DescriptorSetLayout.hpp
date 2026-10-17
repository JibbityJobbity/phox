#ifndef PHOX_VULKAN_DESCRIPTOR_SET_LAYOUT_HPP
#define PHOX_VULKAN_DESCRIPTOR_SET_LAYOUT_HPP

#include "phox/VertexLayout.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox {
namespace vk {

class Device;

class DescriptorSetLayout {
public:
    DescriptorSetLayout(Device* device, const VertexLayout& layout);
    ~DescriptorSetLayout();

    VkDescriptorSetLayout handle() const { return m_layout; }

private:
    Device* m_device = nullptr;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace phox

#endif // PHOX_VULKAN_DESCRIPTOR_SET_LAYOUT_HPP
