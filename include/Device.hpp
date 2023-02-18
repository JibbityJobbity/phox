#ifndef PHOX_DEVICE_HPP
#define PHOX_DEVICE_HPP

#include "Common.hpp"

#include <cstdint>

#include "Platform/Platform.hpp"
#ifndef VK_EXT_debug_utils
#define VK_EXT_debug_utils
#endif // VK_EXT_debug_utils
#include "volk.h"

namespace phox {

enum class DeviceType { Discrete, Integrated, Virtual, CPU, Unknown };

class Device {
  private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VolkDeviceTable m_table = {};
    bool m_initialized = false;

  public:
    Device() = default;
    static VkPhysicalDevice PickPhysicalDevice(VkInstance instance,
                                               DeviceType type);
    void initialize(VkPhysicalDevice physicalDevice, VkInstance instance,
                    VkSurfaceKHR surface);
    bool isInitialized();
    void cleanup();
    ~Device();
};

} // namespace phox

#endif // PHOX_DEVICE_HPP
