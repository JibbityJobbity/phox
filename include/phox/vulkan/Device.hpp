#ifndef PHOX_VULKAN_DEVICE_HPP
#define PHOX_VULKAN_DEVICE_HPP

#include "phox/Device.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <memory>
#include <string>

namespace phox::vk {

class Buffer;
class Image;

/// The logical device, its queues and the memory allocator. Every Vulkan buffer and image in Phox is
/// created here. Objects created from a Device must be destroyed before it.
/// Whether a device has the descriptor indexing features Phox's texture table needs (all of which are
/// enabled on the Device): a partially bound, update-after-bind array of sampled images, indexed
/// non-uniformly.
bool hasBindlessTextures(const VkPhysicalDeviceVulkan12Features &features);

/// Whether a device has what GPU-driven drawing needs (all of which is enabled on the Device): several
/// indirect draws per call, with nonzero firstInstance. Indirect counts are optional (Device::drawIndirectCount).
bool hasGpuDrivenDrawing(const VkPhysicalDeviceFeatures &features);

/// The most textures the texture table (SetLayout::Bindless) holds, whatever the device allows.
inline constexpr std::uint32_t kMaxBindlessTextures = 4096;

class Device final : public phox::Device {
public:
    /// @param surface        VK_NULL_HANDLE when headless; otherwise a queue that can present to it is set up
    /// @param objectNames    VK_EXT_debug_utils is enabled on the instance, so objects can be named
    /// @param meshShaders    enable task and mesh shaders (VK_EXT_mesh_shader) if the device has them
    /// @throws std::runtime_error if the device cannot be created
    Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames,
           bool meshShaders);
    ~Device() override;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    // phox::Device
    std::unique_ptr<phox::Buffer> createBuffer(const BufferCreateInfo &info) override;
    std::unique_ptr<phox::Image> createImage(const ImageCreateInfo &info) override;
    void waitIdle() override;

    /// Same as createBuffer/createImage, typed for code that needs the Vulkan handles.
    std::unique_ptr<Buffer> makeBuffer(const BufferCreateInfo &info);
    std::unique_ptr<Image> makeImage(const ImageCreateInfo &info);


    /// Names an object for debuggers and validation messages. Does nothing without debug utils.
    void setObjectName(VkObjectType type, std::uint64_t handle, const std::string &name) const;
    /// VK_EXT_debug_utils is enabled: objects can be named and command buffers labelled.
    bool debugUtils() const { return m_objectNames; }
    /// VK_EXT_mesh_shader is enabled with task and mesh shaders. Its limits are in meshShaderProperties().
    bool meshShaders() const { return m_meshShaders; }
    const VkPhysicalDeviceMeshShaderPropertiesEXT &meshShaderProperties() const { return m_meshShaderProperties; }
    /// drawIndirectCount is enabled: indirect draws can take their count from a buffer. MoltenVK lacks it
    /// (Metal has no count buffers); PHOX_NO_DRAW_INDIRECT_COUNT=1 turns it off to test that case.
    bool drawIndirectCount() const { return m_drawIndirectCount; }
    /// Size of the texture table: kMaxBindlessTextures, or less if the device allows fewer.
    std::uint32_t bindlessTextureCount() const { return m_bindlessTextureCount; }

    VkDevice handle() const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    const VkPhysicalDeviceProperties &properties() const { return m_properties; }
    VmaAllocator allocator() const { return m_allocator; }
    const VolkDeviceTable &table() const { return m_table; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    std::uint32_t graphicsQueueFamilyIndex() const { return m_graphicsFamily; }
    VkQueue presentQueue() const { return m_presentQueue; }
    std::uint32_t presentQueueFamilyIndex() const { return m_presentFamily; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties{};
    VkPhysicalDeviceMeshShaderPropertiesEXT m_meshShaderProperties{};
    std::uint32_t m_bindlessTextureCount = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VolkDeviceTable m_table{};
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = UINT32_MAX;
    std::uint32_t m_presentFamily = UINT32_MAX;
    bool m_objectNames = false;
    bool m_meshShaders = false;
    bool m_drawIndirectCount = false;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_DEVICE_HPP
