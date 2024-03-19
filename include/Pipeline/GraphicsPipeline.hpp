#ifndef PHOX_GRAPHICS_PIPELINE_HPP
#define PHOX_GRAPHICS_PIPELINE_HPP

#include "./Pipeline.hpp"

namespace phox {

class GraphicsPipeline : Pipeline {
  private:
    VkPipeline m_pipeline;
    VkShaderModule m_vertexShader;
    VkShaderModule m_fragmentShader;

  public:
    GraphicsPipeline(VkDevice device);
    void addShader(VkShaderModule shader, VkShaderStageFlagBits stage);
    virtual void compile();
    void cleanup();
};

} // namespace phox

#endif // PHOX_GRAPHICS_PIPELINE_HPP