#ifndef PHOX_VULKAN_SWAPCHAIN_HPP
#define PHOX_VULKAN_SWAPCHAIN_HPP

#include "phox/Swapchain.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <vector>

namespace phox {
namespace vk {

class Device;

/// A presentable set of images for a surface. Constructing one without a surface (headless)
/// yields an inert object for which valid() is false.
class Swapchain : public phox::Swapchain {
public:
    /// @param width,height  requested size; used only when the surface does not dictate one
    /// @throws std::runtime_error if the surface/device cannot support a swapchain
    Swapchain(Device* device, VkSurfaceKHR surface, uint32_t width, uint32_t height);
    ~Swapchain();
    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    bool valid() const { return m_swapchain != VK_NULL_HANDLE; }
    VkSwapchainKHR handle() const { return m_swapchain; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    const std::vector<VkImage>& images() const { return m_images; }

    /// Acquires the next image, signalling @p signal when it is ready.
    /// @return false if the swapchain is out of date and must be recreated (nothing acquired).
    bool acquireNextImage(VkSemaphore signal, uint32_t &imageIndex);

    /// Presents @p imageIndex once @p wait is signalled.
    /// @return false if the swapchain is out of date or suboptimal and should be recreated.
    bool present(uint32_t imageIndex, VkSemaphore wait);

private:
    Device* m_device = nullptr;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
};

} // namespace vk
} // namespace phox

#endif // PHOX_VULKAN_SWAPCHAIN_HPP
