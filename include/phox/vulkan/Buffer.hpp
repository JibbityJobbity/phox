#ifndef PHOX_VULKAN_BUFFER_HPP
#define PHOX_VULKAN_BUFFER_HPP

#include "phox/Buffer.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox::vk {

class Device;

class Buffer final : public phox::Buffer {
public:
    Buffer(Device &device, VkBuffer buffer, VmaAllocation allocation, std::uint64_t size, void *mapped,
           VkDeviceAddress deviceAddress = 0);
    ~Buffer() override;

    VkBuffer handle() const { return m_buffer; }
    /// The buffer's address for shaders. 0 unless it was created with BufferCreateInfo::deviceAddress.
    VkDeviceAddress deviceAddress() const { return m_deviceAddress; }
    void *mapped() const override { return m_mapped; }
    void flush(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) override;
    void invalidate(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) override;

private:
    Device &m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
    void *m_mapped = nullptr;
    VkDeviceAddress m_deviceAddress = 0;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_BUFFER_HPP
