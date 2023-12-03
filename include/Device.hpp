#ifndef PHOX_DEVICE_HPP
#define PHOX_DEVICE_HPP

#include "Common.hpp"

#include "Buffer.hpp"
#include "Image.hpp"
#include "Pipeline/Shader.hpp"
#include "Platform/Platform.hpp"

namespace phox {

enum class MemoryLocation { Auto, Device, Host, HostVisible };

enum class DeviceType { Discrete, Integrated, Virtual, CPU, Unknown };

class Device {
  private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    struct {
        uint32_t presentQueueIndex = -1, graphicsQueueIndex = -1;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
    } m_queueInfo;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VolkDeviceTable m_table = {};
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    bool m_initialized = false;

  public:
    Device() = default;
    static VkPhysicalDevice PickPhysicalDevice(VkInstance instance,
                                               DeviceType type);
    void initialize(VkPhysicalDevice physicalDevice, VkInstance instance,
                    VkSurfaceKHR surface);
    Buffer allocateBuffer(size_t size, VkBufferUsageFlagBits usage,
                          MemoryLocation memoryLocation);
    Image allocateImage(size_t width, size_t height);
    Buffer copyBuffer(Buffer &buffer, VkBufferUsageFlagBits usage,
                      MemoryLocation memoryLocation);
    Shader loadSpirv(const char *path);
    void freeBuffer(Buffer &buffer);
    bool isInitialized();
    void cleanup();
    ~Device();
};

} // namespace phox

#endif // PHOX_DEVICE_HPP
