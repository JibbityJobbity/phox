#ifndef PHOX_RENDERER_HPP
#define PHOX_RENDERER_HPP

#include <vector>

#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#include <GLFW/glfw3.h>

#include "Device.hpp"

namespace phox {

class Renderer {
  private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    phox::Device m_device;
    bool m_enableValidationLayers = false;
    bool m_initialized = false;

    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow *window);
    void createLogicalDevice();

  public:
    Renderer() = default;
    void initialize(GLFWwindow *window, bool debug);
    void cleanup();
    ~Renderer();
};

} // namespace phox

#endif // PHOX_RENDERER_HPP