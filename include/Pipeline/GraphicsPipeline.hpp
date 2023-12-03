#ifndef PHOX_GRAPHICS_PIPELINE_HPP
#define PHOX_GRAPHICS_PIPELINE_HPP

#include "./Pipeline.hpp"

namespace phox {

class GraphicsPipeline : Pipeline {
  private:
    VkPipeline m_pipeline;

  public:
    void addShader(VkShaderModule shader, VkShaderStageFlagBits stage);
    void cleanup();
};

} // namespace phox

#endif // PHOX_GRAPHICS_PIPELINE_HPP