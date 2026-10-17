#ifndef PHOX_VULKAN_RENDERER_HPP
#define PHOX_VULKAN_RENDERER_HPP

#include <vector>

namespace phox {
  namespace vk {
    class Device;
  }
};

#include "phox/Renderer.hpp"
#include "phox/vulkan/DescriptorSetLayout.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/Swapchain.hpp"
#include "phox/vulkan/VulkanCommon.hpp"


namespace phox {

namespace vk {

class Renderer : public phox::Renderer {
  private:
    std::unique_ptr<Device> m_device;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<DescriptorSetLayout> m_descriptorSetLayout;
    bool m_enableValidationLayers = false;
    bool m_initialized = false;
    bool m_useFallback = false;

    void createInstance();
    void renderFallback(const Scene &scene);
    void setupDebugMessenger();
    void createSurface();
    void createLogicalDevice();

  public:
    Renderer();
    void initialize(bool debug) override;
    void cleanup() override;
    void render(const Scene &scene) override;
    ~Renderer() override;
};

}

} // namespace phox

#endif // PHOX_VULKAN_RENDERER_HPP