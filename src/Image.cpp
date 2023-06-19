#include "Image.hpp"

using namespace phox;

Image::Image(VkImage image, VkDevice device, VmaAllocator allocator,
             VmaAllocation allocation)
    : m_image(image), m_device(device), m_allocator(allocator),
      m_allocation(allocation) {}

Image::~Image() { vmaDestroyImage(m_allocator, m_image, m_allocation); }
