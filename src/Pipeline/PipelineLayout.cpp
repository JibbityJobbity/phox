#include "Pipeline/PipelineLayout.hpp"
#include "Pipeline/GraphicsPipeline.hpp"
#include <vulkan/vulkan_core.h>

using namespace phox;

PipelineLayout::PipelineLayout(VkDevice device)
    : m_device(device), m_pipelineLayout(VK_NULL_HANDLE),
      m_descriptorSetLayout(VK_NULL_HANDLE) {}

void PipelineLayout::setDescriptors(
    const std::vector<VkDescriptorSetLayoutBinding> &layouts) {
    VkDescriptorSetLayoutCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = layouts.size();
    createInfo.pBindings = layouts.data();
    vkCreateDescriptorSetLayout(m_device, &createInfo, nullptr,
                                &m_descriptorSetLayout);
}

void PipelineLayout::compile() {
    VkDescriptorSetLayout setLayouts[] = {m_descriptorSetLayout};
    VkPipelineLayoutCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.pSetLayouts = &m_descriptorSetLayout;
    createInfo.setLayoutCount = 1;
    createInfo.pushConstantRangeCount = 0;

    vkCreatePipelineLayout(m_device, &createInfo, nullptr, &m_pipelineLayout);
}

VkPipelineLayout PipelineLayout::getPipelineLayout() {
    if (m_pipelineLayout == VK_NULL_HANDLE) {
        compile();
    }
    return m_pipelineLayout;
}
