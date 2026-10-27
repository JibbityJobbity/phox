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
class Device final : public phox::Device {
public:
    /// @param surface        VK_NULL_HANDLE when headless; otherwise a queue that can present to it is set up
    /// @param objectNames    VK_EXT_debug_utils is enabled on the instance, so objects can be named
    /// @throws std::runtime_error if the device cannot be created
    Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames);
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
    VkDevice m_device = VK_NULL_HANDLE;
    VolkDeviceTable m_table{};
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = UINT32_MAX;
    std::uint32_t m_presentFamily = UINT32_MAX;
    bool m_objectNames = false;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_DEVICE_HPP
