#ifndef PHOX_VULKAN_DESCRIPTOR_ALLOCATOR_HPP
#define PHOX_VULKAN_DESCRIPTOR_ALLOCATOR_HPP

#include "phox/vulkan/VulkanCommon.hpp"

#include <vector>

namespace phox::vk {

class Device;

/// Descriptor sets for one frame. Allocates from a list of pools that grows when one runs out; reset()
/// frees every set at once and may only be called once the GPU has finished the frame.
class DescriptorAllocator {
public:
    explicit DescriptorAllocator(Device &device);
    ~DescriptorAllocator();
    DescriptorAllocator(const DescriptorAllocator &) = delete;
    DescriptorAllocator &operator=(const DescriptorAllocator &) = delete;

    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void reset();

private:
    VkDescriptorPool createPool();

    Device &m_device;
    std::vector<VkDescriptorPool> m_pools; ///< m_pools[m_current] is the one being allocated from
    std::size_t m_current = 0;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_DESCRIPTOR_ALLOCATOR_HPP
