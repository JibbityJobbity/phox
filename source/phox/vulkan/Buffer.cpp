#include "phox/vulkan/Buffer.hpp"

#include "phox/vulkan/Device.hpp"

namespace phox::vk {

Buffer::Buffer(Device &device, VkBuffer buffer, VmaAllocation allocation, std::uint64_t size, void *mapped)
    : phox::Buffer(size), m_device(device), m_buffer(buffer), m_allocation(allocation), m_mapped(mapped) {}

Buffer::~Buffer() { vmaDestroyBuffer(m_device.allocator(), m_buffer, m_allocation); }

void Buffer::flush(std::uint64_t offset, std::uint64_t size) {
    vmaFlushAllocation(m_device.allocator(), m_allocation, offset, size == UINT64_MAX ? VK_WHOLE_SIZE : size);
}

void Buffer::invalidate(std::uint64_t offset, std::uint64_t size) {
    vmaInvalidateAllocation(m_device.allocator(), m_allocation, offset, size == UINT64_MAX ? VK_WHOLE_SIZE : size);
}

} // namespace phox::vk
