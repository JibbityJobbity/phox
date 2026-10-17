#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/Device.hpp"

using namespace phox;

vk::GraphicsPipeline::GraphicsPipeline(Device* device) : m_device(device) {}

vk::GraphicsPipeline::~GraphicsPipeline() {
    if (m_device == nullptr || m_device->handle() == VK_NULL_HANDLE) {
        return;
    }
    const VkDevice dev = m_device->handle();
    const auto &table = m_device->table();
    // Reverse of creation order; destroying a null handle is a no-op.
    table.vkDestroyPipeline(dev, m_pipeline, nullptr);
    table.vkDestroyRenderPass(dev, m_renderPass, nullptr);
    table.vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
    m_descriptorSetLayout.reset();
    m_pipeline = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
}
