#ifndef PHOX_VULKAN_DEVICE_HPP
#define PHOX_VULKAN_DEVICE_HPP

#include "phox/Device.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <array>
#include <cstdint>
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

/// The most textures the texture table (SetLayout::Bindless) holds, whatever the device allows.
inline constexpr std::uint32_t kMaxBindlessTextures = 4096;

class Device final : public phox::Device {
public:
    /// @param surface        VK_NULL_HANDLE when headless; otherwise a queue that can present to it is set up
    /// @param objectNames    VK_EXT_debug_utils is enabled on the instance, so objects can be named
    /// @param meshShaders    enable task and mesh shaders (VK_EXT_mesh_shader) if the device has them
    /// @param asyncCompute   set up a queue from a compute-only family, if the device has one
    /// @throws std::runtime_error if the device cannot be created
    Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames,
           bool meshShaders, bool asyncCompute = true);
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

    /// There is a queue for Queue::AsyncCompute, from a family that cannot draw. Without one, render graphs
    /// run their async passes on the graphics queue. Buffers are created for both queues when there is one.
    bool asyncCompute() const { return m_computeQueue != VK_NULL_HANDLE; }
    /// @throws std::logic_error for Queue::AsyncCompute without asyncCompute()
    VkQueue queue(Queue queue) const;
    std::uint32_t queueFamilyIndex(Queue queue) const;
    /// Timestamp queries can be reset from the CPU (hostQueryReset).
    bool hostQueryReset() const { return m_hostQueryReset; }
    /// Each queue's timeline semaphore. Every submission to a queue signals its timeline with the next value.
    VkSemaphore timeline(Queue queue) const { return m_timelines[static_cast<std::size_t>(queue)].semaphore; }
    /// The value the next submission to @p queue signals; each call returns a larger one.
    std::uint64_t signalValue(Queue queue);
    /// Blocks until @p queue's timeline reaches @p value (0: returns at once).
    void waitTimeline(Queue queue, std::uint64_t value) const;

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
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = UINT32_MAX;
    std::uint32_t m_presentFamily = UINT32_MAX;
    std::uint32_t m_computeFamily = UINT32_MAX;
    struct QueueTimeline {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        std::uint64_t value = 0; ///< the last value a submission was given
    };
    std::array<QueueTimeline, 2> m_timelines{}; ///< indexed by Queue
    bool m_hostQueryReset = false;
    bool m_objectNames = false;
    bool m_meshShaders = false;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_DEVICE_HPP
