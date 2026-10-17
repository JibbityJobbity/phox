#include "phox/vulkan/Swapchain.hpp"

#include "phox/vulkan/Device.hpp"

namespace phox {
namespace vk {

Swapchain::Swapchain(Device* device, VkSurfaceKHR surface)
    : m_device(device) {
    if (m_device == nullptr || m_device->handle() == VK_NULL_HANDLE ||
        surface == VK_NULL_HANDLE) {
        // Headless: no surface, nothing to create.
        return;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = 2;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = {1280, 720};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    if (m_device->table().vkCreateSwapchainKHR != nullptr) {
        m_device->table().vkCreateSwapchainKHR(m_device->handle(), &createInfo, nullptr, &m_swapchain);
    }
}

Swapchain::~Swapchain() {
    if (m_swapchain != VK_NULL_HANDLE && m_device != nullptr && m_device->handle() != VK_NULL_HANDLE &&
        m_device->table().vkDestroySwapchainKHR != nullptr) {
        m_device->table().vkDestroySwapchainKHR(m_device->handle(), m_swapchain, nullptr);
    }
}

} // namespace vk
} // namespace phox
