#include "phox/vulkan/DescriptorSetLayout.hpp"

#include "phox/vulkan/Device.hpp"

#include <stdexcept>

namespace phox {
namespace vk {

DescriptorSetLayout::DescriptorSetLayout(Device* device, const VertexLayout& layout)
    : m_device(device) {
    (void)layout;
    if (m_device == nullptr || m_device->handle() == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorSetLayout requires an initialized device");
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back(cameraBinding);

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    if (m_device->table().vkCreateDescriptorSetLayout(m_device->handle(), &createInfo, nullptr, &m_layout) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (m_layout != VK_NULL_HANDLE && m_device != nullptr && m_device->handle() != VK_NULL_HANDLE &&
        m_device->table().vkDestroyDescriptorSetLayout != nullptr) {
        m_device->table().vkDestroyDescriptorSetLayout(m_device->handle(), m_layout, nullptr);
    }
}

} // namespace vk
} // namespace phox
