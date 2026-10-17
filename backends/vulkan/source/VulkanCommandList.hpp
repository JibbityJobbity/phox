#ifndef PHOX_VK_VULKAN_COMMAND_LIST_HPP
#define PHOX_VK_VULKAN_COMMAND_LIST_HPP

#include "VulkanDevice.hpp"

#include "phox/rhi/CommandList.hpp"

#include <map>
#include <set>
#include <vector>

namespace phox::vulkan {

class VulkanCommandList final : public rhi::CommandList {
  public:
    VulkanCommandList(VulkanDevice &device, VkCommandBuffer commandBuffer);

    VkCommandBuffer handle() const { return m_cb; }
    void end();
    std::vector<VkDescriptorPool> takePools() { return std::move(m_pools); }

    void barrier(std::span<const rhi::TextureBarrier>, std::span<const rhi::BufferBarrier>) override;
    void beginRendering(const rhi::RenderingInfo &) override;
    void endRendering() override;
    void bindPipeline(rhi::PipelineHandle) override;
    void bindVertexBuffer(rhi::BufferHandle, uint64_t offset) override;
    void bindIndexBuffer(rhi::BufferHandle, uint64_t offset) override;
    void bindUniformBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle) override;
    void bindStorageBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle) override;
    void bindSampledTexture(uint32_t set, uint32_t binding, rhi::TextureHandle) override;
    void pushConstants(rhi::ShaderStage, uint32_t offset, std::span<const std::byte>) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) override;
    void drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void copyBuffer(rhi::BufferHandle src, rhi::BufferHandle dst, uint64_t size) override;
    void copyTextureToBuffer(rhi::TextureHandle src, rhi::BufferHandle dst) override;
    void beginDebugLabel(const char *name) override;
    void endDebugLabel() override;

  private:
    struct Pending {
        rhi::BufferHandle buffer;
        rhi::TextureHandle texture;
    };

    const PipelineData &boundPipeline() const;
    void flushDescriptors();
    VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);

    VulkanDevice &m_device;
    VkCommandBuffer m_cb;
    rhi::PipelineHandle m_pipeline;
    std::map<uint32_t, std::map<uint32_t, Pending>> m_pending; // set -> binding -> resource
    std::set<uint32_t> m_dirty;
    std::vector<VkDescriptorPool> m_pools;
};

} // namespace phox::vulkan

#endif // PHOX_VK_VULKAN_COMMAND_LIST_HPP
