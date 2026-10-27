#include "phox/vulkan/DescriptorAllocator.hpp"

#include "phox/vulkan/Device.hpp"

#include <array>
#include <stdexcept>

namespace phox::vk {

DescriptorAllocator::DescriptorAllocator(Device &device) : m_device(device) {}

DescriptorAllocator::~DescriptorAllocator() {
    for (VkDescriptorPool pool : m_pools) {
        m_device.table().vkDestroyDescriptorPool(m_device.handle(), pool, nullptr);
    }
}

VkDescriptorPool DescriptorAllocator::createPool() {
    constexpr std::uint32_t kSets = 64;
    const std::array<VkDescriptorPoolSize, 5> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSets},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSets / 4},
    }};
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = kSets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (m_device.table().vkCreateDescriptorPool(m_device.handle(), &info, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create a descriptor pool");
    }
    return pool;
}

VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
    for (;;) {
        if (m_current == m_pools.size()) {
            m_pools.push_back(createPool());
        }
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = m_pools[m_current];
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult result = m_device.table().vkAllocateDescriptorSets(m_device.handle(), &info, &set);
        if (result == VK_SUCCESS) {
            return set;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            throw std::runtime_error("failed to allocate a descriptor set");
        }
        ++m_current; // this pool is full; move to the next (creating it if needed)
    }
}

void DescriptorAllocator::reset() {
    for (VkDescriptorPool pool : m_pools) {
        m_device.table().vkResetDescriptorPool(m_device.handle(), pool, 0);
    }
    m_current = 0;
}

} // namespace phox::vk
