#include "phox/vulkan/Buffer.hpp"

namespace phox {

namespace vk {

Buffer::Buffer(Device *device, VkDeviceSize size, VkBuffer buffer,
    VmaAllocation allocation)
    : phox::Buffer(size), m_device(device), m_buffer(buffer), m_allocation(allocation) {}

void *Buffer::map() {
    if (m_ptr)
        return m_ptr;
    vmaMapMemory(m_device->m_allocator, m_allocation, &m_ptr);
    return m_ptr;
}

void Buffer::unmap() {
    if (!m_ptr)
        return;
    if (m_device == nullptr || m_device->m_allocator == VK_NULL_HANDLE) {
        m_ptr = nullptr;
        return;
    }
    vmaUnmapMemory(m_device->m_allocator, m_allocation);

    m_ptr = nullptr;
}

Buffer::~Buffer() {
    unmap();
    free();
}

void Buffer::free() {
    if (m_buffer == VK_NULL_HANDLE || m_allocation == nullptr) {
        return;
    }
    if (m_device == nullptr || m_device->m_allocator == VK_NULL_HANDLE) {
        // Allocator already torn down; the memory went with it.
        m_buffer = VK_NULL_HANDLE;
        m_allocation = nullptr;
        m_ptr = nullptr;
        return;
    }
    vmaDestroyBuffer(m_device->m_allocator, m_buffer, m_allocation);
    m_buffer = VK_NULL_HANDLE;
    m_allocation = nullptr;
    m_ptr = nullptr;
}

}

} // namespace phox