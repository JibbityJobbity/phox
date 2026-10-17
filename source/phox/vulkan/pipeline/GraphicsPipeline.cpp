#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/Device.hpp"

using namespace phox;

vk::GraphicsPipeline::GraphicsPipeline(Device* device, VkPipeline pipeline)
    : m_device(device), m_pipeline(pipeline) {}

vk::GraphicsPipeline::~GraphicsPipeline() {
    if (m_pipeline == VK_NULL_HANDLE || m_device == nullptr ||
        m_device->m_device == VK_NULL_HANDLE) {
        return;
    }
    m_device->m_table.vkDestroyPipeline(m_device->m_device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;
}
