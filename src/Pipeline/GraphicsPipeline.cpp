#include "Pipeline/GraphicsPipeline.hpp"

#include <stdexcept>
#include <vector>

using namespace phox;
using namespace std;

GraphicsPipeline::GraphicsPipeline(VkDevice device) : Pipeline(device) {}

void GraphicsPipeline::addShader(VkShaderModule shader,
                                 VkShaderStageFlagBits stage) {
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        m_vertexShader = shader;
    } else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        m_fragmentShader = shader;
    } else {
        throw runtime_error("Unsupported shader stage");
    }
}

void GraphicsPipeline::compile() {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    shaderStages.push_back({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                            m_vertexShader, "main", nullptr});
    shaderStages.push_back({VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                            m_fragmentShader, "main", nullptr});

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();

    vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                              nullptr, &m_pipeline);
}
