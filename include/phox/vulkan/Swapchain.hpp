#ifndef PHOX_VULKAN_SWAPCHAIN_HPP
#define PHOX_VULKAN_SWAPCHAIN_HPP

#include "phox/Swapchain.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox {
namespace vk {

class Device;

class Swapchain : public phox::Swapchain {
public:
    Swapchain(Device* device, VkSurfaceKHR surface);
    ~Swapchain();

    VkSwapchainKHR handle() const { return m_swapchain; }

private:
    Device* m_device = nullptr;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace phox

#endif // PHOX_VULKAN_SWAPCHAIN_HPP
