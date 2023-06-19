#ifndef PHOX_BUFFER_HPP
#define PHOX_BUFFER_HPP

#include "Common.hpp"

namespace phox {

class Buffer {
  public:
    Buffer(VkDeviceSize size, VkBuffer buffer, VmaAllocator allocator,
           VmaAllocation allocation);
    void *map();
    void unmap();
    void free();

  private:
    void *m_ptr = nullptr;
    VkDeviceSize m_size = 0;
    VmaAllocator m_allocator = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
};

} // namespace phox

#endif // FLUID_SIM_BUFFER_HPP
