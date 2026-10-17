#ifndef PHOX_VK_VULKAN_DEVICE_HPP
#define PHOX_VK_VULKAN_DEVICE_HPP

#include "Pool.hpp"
#include "VkCommon.hpp"

#include "phox/rhi/Backend.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phox::vulkan {

class VulkanCommandList;

struct BufferData {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void *mapped = nullptr;
    rhi::BufferDesc desc{};
};

struct TextureData {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    rhi::TextureDesc desc{};
};

struct ShaderData {
    VkShaderModule module = VK_NULL_HANDLE;
    rhi::ShaderStage stage{};
    std::string entry;
};

struct DescriptorBinding {
    uint32_t set = 0, binding = 0;
    rhi::BindingType type{};
};

struct PipelineData {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> setLayouts; // one per set index 0..maxSet (gaps are empty layouts)
    std::vector<DescriptorBinding> bindings;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    bool pushDescriptors = false; // the single set 0 is a push-descriptor set
};

class VulkanDevice final : public rhi::Device {
  public:
    explicit VulkanDevice(const rhi::DeviceOptions &options);
    ~VulkanDevice() override;
    VulkanDevice(const VulkanDevice &) = delete;
    VulkanDevice &operator=(const VulkanDevice &) = delete;

    const rhi::Capabilities &capabilities() const override { return m_capabilities; }

    rhi::BufferHandle createBuffer(const rhi::BufferDesc &) override;
    rhi::TextureHandle createTexture(const rhi::TextureDesc &) override;
    rhi::ShaderHandle createShader(const rhi::ShaderDesc &) override;
    rhi::PipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDesc &) override;
    rhi::PipelineHandle createComputePipeline(const rhi::ComputePipelineDesc &) override;
    void destroy(rhi::BufferHandle) override;
    void destroy(rhi::TextureHandle) override;
    void destroy(rhi::ShaderHandle) override;
    void destroy(rhi::PipelineHandle) override;

    std::span<std::byte> map(rhi::BufferHandle) override;
    const rhi::TextureDesc &describe(rhi::TextureHandle) const override;
    const rhi::BufferDesc &describe(rhi::BufferHandle) const override;

    rhi::CommandList &beginCommands() override;
    rhi::SubmitTicket submit(rhi::CommandList &) override;
    void wait(rhi::SubmitTicket) override;
    void waitIdle() override;

    // ---- used by VulkanCommandList ----
    const VolkDeviceTable &table() const { return m_table; }
    VkDevice handle() const { return m_device; }
    bool debugUtilsEnabled() const { return m_debugUtils; }
    const BufferData &bufferData(rhi::BufferHandle h) const { return m_buffers.get(h); }
    const TextureData &textureData(rhi::TextureHandle h) const { return m_textures.get(h); }
    const PipelineData &pipelineData(rhi::PipelineHandle h) const { return m_pipelines.get(h); }
    VkDescriptorPool acquireDescriptorPool();

  private:
    struct InFlight {
        uint64_t ticket;
        std::unique_ptr<VulkanCommandList> list;
    };
    struct Deferred {
        uint64_t ticket;
        std::function<void()> destroy;
    };

    void init(const rhi::DeviceOptions &options);
    void shutdown() noexcept;
    void createInstance(const rhi::DeviceOptions &options);
    VkPhysicalDevice pickPhysicalDevice(const rhi::DeviceOptions &options);
    void createLogicalDevice();
    void createAllocator();

    PipelineData buildPipelineLayout(const std::vector<rhi::Binding> &bindings,
                                     const std::vector<rhi::PushConstantRange> &pushConstants, bool compute);
    void destroyPipelineData(PipelineData &data);
    void destroyBufferData(BufferData &data);
    void destroyTextureData(TextureData &data);

    const char *keepName(const char *name);
    void setObjectName(VkObjectType type, uint64_t handle, const char *name);
    uint64_t completedTicket() const;
    void collect();
    void defer(std::function<void()> destroy);
    void retire(InFlight &entry);
    void flushUploads();
    void invalidateReadbacks();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VolkDeviceTable m_table{};
    VmaAllocator m_allocator = nullptr;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
    bool m_debugUtils = false;
    bool m_pushDescriptors = false;
    bool m_meshShaders = false;

    rhi::Capabilities m_capabilities;
    Pool<rhi::BufferHandle, BufferData> m_buffers;
    Pool<rhi::TextureHandle, TextureData> m_textures;
    Pool<rhi::ShaderHandle, ShaderData> m_shaders;
    Pool<rhi::PipelineHandle, PipelineData> m_pipelines;
    std::deque<std::string> m_names; // stable storage so describe() can hand back debug names

    std::unique_ptr<VulkanCommandList> m_active;
    std::vector<InFlight> m_inFlight;
    std::vector<Deferred> m_deferred;
    std::vector<VkDescriptorPool> m_freeDescriptorPools;
    std::vector<VkDescriptorPool> m_allDescriptorPools;
    uint64_t m_lastTicket = 0;
};

} // namespace phox::vulkan

#endif // PHOX_VK_VULKAN_DEVICE_HPP
