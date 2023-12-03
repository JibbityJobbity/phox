#include "Pipeline/GraphicsPipeline.hpp"

using namespace phox;
using namespace std;

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
