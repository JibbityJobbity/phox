#ifndef PHOX_VULKAN_RENDERER_HPP
#define PHOX_VULKAN_RENDERER_HPP

#include <vector>

namespace phox {
  namespace vk {
    class Device;
  }
};

#include "phox/Renderer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/Swapchain.hpp"
#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/pipeline/Shader.hpp"
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
    // Created once in initialize(); render() only builds per-frame objects.
    std::unique_ptr<Shader> m_vertShader;
    std::unique_ptr<Shader> m_fragShader;
    std::unique_ptr<GraphicsPipeline> m_pipeline;
    bool m_enableValidationLayers = false;
    bool m_initialized = false;
    bool m_useFallback = false;

    void createInstance();
    void renderFallback(const Scene &scene);
    void setupDebugMessenger();
    void createSurface();
    void createLogicalDevice();
    void createPipeline();

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