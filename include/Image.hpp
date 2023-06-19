#ifndef PHOX_IMAGE_HPP
#define PHOX_IMAGE_HPP

#include "Common.hpp"

namespace phox {

class Image {
  private:
    VkDevice m_device;
    VkImage m_image;
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    Image() = default;

  public:
    Image(VkImage image, VkDevice device, VmaAllocator allocator,
          VmaAllocation allocation);
    ~Image();
};

} // namespace phox

#endif // PHOX_IMAGE_HPP