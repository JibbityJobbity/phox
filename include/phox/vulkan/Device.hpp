#ifndef PHOX_VULKAN_DEVICE_HPP
#define PHOX_VULKAN_DEVICE_HPP

namespace phox {
  namespace vk {
    class Renderer;
    class Buffer;
    class Image;
    class Model;
    class Shader;
    class GraphicsPipeline;
  }
};

#include "phox/Common.hpp"

#include "phox/Device.hpp"
#include "phox/vulkan/Renderer.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/pipeline/Pipeline.hpp"
#include "phox/vulkan/pipeline/Shader.hpp"
#include "phox/vulkan/Image.hpp"

namespace phox {

namespace vk {

class Device : public phox::Device {
  friend class Buffer;
  friend class Image;
  friend class Shader;
  friend class GraphicsPipeline;
  private:
    Renderer* m_renderer;
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

    VkPhysicalDevice pickPhysicalDevice(const DeviceType type);
  protected:
    const VolkDeviceTable* deviceTable();

  public:
    Device() = default;
    void Initialize(VkInstance instance, VkPhysicalDevice phyDdev,
                    VkSurfaceKHR surface);
    std::unique_ptr<phox::Buffer> allocateBuffer(const size_t size, const MemoryLocation memoryLocation) override;
    std::unique_ptr<phox::Image> allocateImage(const size_t width, const size_t height) override;
    std::unique_ptr<phox::Buffer> copyBuffer(phox::Buffer &buffer, MemoryLocation memoryLocation) override;
    std::unique_ptr<phox::Pipeline> createPipeline(const std::vector<phox::Shader*>& shaders) override;
    void freeBuffer(phox::Buffer &buffer) override;
    bool isInitialized() override;
    void cleanup() override;
    VkDevice handle() const { return m_device; }
    VmaAllocator allocator() const { return m_allocator; }
    const VolkDeviceTable& table() const { return m_table; }
    VkQueue graphicsQueue() const { return m_queueInfo.graphicsQueue; }
    uint32_t graphicsQueueFamilyIndex() const { return m_queueInfo.graphicsQueueIndex; }
    ~Device();
};

}

} // namespace phox

#endif // PHOX_VULKAN_DEVICE_HPP
