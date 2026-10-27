#include "phox/vulkan/Swapchain.hpp"

#include "phox/vulkan/Device.hpp"

#include <algorithm>
#include <stdexcept>

namespace phox {
namespace vk {

Swapchain::Swapchain(Device &device, VkSurfaceKHR surface, uint32_t width, uint32_t height,
                     const Swapchain *previous)
    : m_device(device) {
    const VkPhysicalDevice physicalDevice = m_device.physicalDevice();

    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps) != VK_SUCCESS) {
        throw std::runtime_error("Failed to query surface capabilities");
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount == 0) {
        throw std::runtime_error("Surface reports no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    // The tonemap pass writes display values itself, so prefer UNORM (an sRGB format would encode them
    // a second time); accept the sRGB 8-bit formats otherwise. Phox has names only for these.
    const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB,
                                  VK_FORMAT_R8G8B8A8_SRGB};
    const VkSurfaceFormatKHR *chosenPtr = nullptr;
    for (VkFormat want : preferred) {
        for (const auto &f : formats) {
            if (f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosenPtr = &f;
                break;
            }
        }
        if (chosenPtr != nullptr) break;
    }
    if (chosenPtr == nullptr) {
        throw std::runtime_error("the surface offers no 8-bit RGBA/BGRA format");
    }
    const VkSurfaceFormatKHR chosen = *chosenPtr;

    // FIFO is the only present mode every implementation must support.
    const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR candidate :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & candidate) {
            compositeAlpha = candidate;
            break;
        }
    }

    const uint32_t queueFamilies[] = {m_device.graphicsQueueFamilyIndex(), m_device.presentQueueFamilyIndex()};

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosen.format;
    createInfo.imageColorSpace = chosen.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (queueFamilies[0] != queueFamilies[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilies;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = previous != nullptr ? previous->handle() : VK_NULL_HANDLE;

    const auto &table = m_device.table();
    if (table.vkCreateSwapchainKHR(m_device.handle(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    uint32_t nImages = 0;
    table.vkGetSwapchainImagesKHR(m_device.handle(), m_swapchain, &nImages, nullptr);
    m_images.resize(nImages);
    table.vkGetSwapchainImagesKHR(m_device.handle(), m_swapchain, &nImages, m_images.data());

    m_format = chosen.format;
    m_extent = extent;

    for (VkImage image : m_images) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        if (table.vkCreateImageView(m_device.handle(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
            for (VkImageView created : m_views) table.vkDestroyImageView(m_device.handle(), created, nullptr);
            table.vkDestroySwapchainKHR(m_device.handle(), m_swapchain, nullptr);
            throw std::runtime_error("failed to create a swapchain image view");
        }
        m_views.push_back(view);
    }
}

Swapchain::~Swapchain() {
    for (VkImageView view : m_views) {
        m_device.table().vkDestroyImageView(m_device.handle(), view, nullptr);
    }
    m_device.table().vkDestroySwapchainKHR(m_device.handle(), m_swapchain, nullptr);
}

bool Swapchain::acquireNextImage(VkSemaphore signal, uint32_t &imageIndex) {
    const VkResult result = m_device.table().vkAcquireNextImageKHR(
        m_device.handle(), m_swapchain, UINT64_MAX, signal, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    // SUBOPTIMAL still acquired an image (and will signal the semaphore); present() reports it.
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }
    return true;
}

bool Swapchain::present(uint32_t imageIndex, VkSemaphore wait) {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &imageIndex;

    const VkResult result = m_device.table().vkQueuePresentKHR(m_device.presentQueue(), &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }
    return true;
}

} // namespace vk
} // namespace phox
