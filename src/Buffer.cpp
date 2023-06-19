#include "Buffer.hpp"

namespace phox {

Buffer::Buffer(VkDeviceSize size, VkBuffer buffer, VmaAllocator allocator,
               VmaAllocation allocation)
    : m_size(size), m_buffer(buffer), m_allocator(allocator),
      m_allocation(allocation) {}

void *Buffer::map() {
    if (m_ptr)
        return m_ptr;
    vmaMapMemory(m_allocator, m_allocation, &m_ptr);
    return m_ptr;
}

void Buffer::unmap() {
    if (!m_ptr)
        return;
    vmaUnmapMemory(m_allocator, m_allocation);

    m_ptr = nullptr;
}

void Buffer::free() { vmaDestroyBuffer(m_allocator, m_buffer, m_allocation); }

} // namespace phox