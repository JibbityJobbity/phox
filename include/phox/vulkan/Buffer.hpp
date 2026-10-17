#ifndef PHOX_VULKAN_BUFFER_HPP
#define PHOX_VULKAN_BUFFER_HPP

namespace phox {
  namespace vk {
    class Device;
    class Buffer;
  }
};

#include "phox/Common.hpp"
#include "phox/Buffer.hpp"
#include "phox/vulkan/VulkanCommon.hpp"
#include "phox/vulkan/Device.hpp"

namespace phox {

namespace vk {

class Buffer : public phox::Buffer {
  friend class Device;
  public:
    Buffer(Device *device, VkDeviceSize size, VkBuffer buffer, VmaAllocation allocation);
    VkBuffer handle() const { return m_buffer; }
    VmaAllocation allocation() const { return m_allocation; }
    void *map() override;
    void unmap() override;
    void free() override;
    ~Buffer() override;

  private:
    void *m_ptr = nullptr;
    Device *m_device = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
};

}

} // namespace phox

#endif // PHOX_VULKAN_BUFFER_HPP
