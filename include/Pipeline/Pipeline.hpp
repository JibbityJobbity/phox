#ifndef PHOX_PIPELINE_HPP
#define PHOX_PIPELINE_HPP

#include "../Common.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "Shader.hpp"

namespace phox {
class Pipeline {
  private:
    Pipeline() = default;
    ~Pipeline() = default;
    VkPipeline m_pipeline;

  public:
    virtual void addShader(Shader shader, VkShaderStageFlagBits stage);
    virtual void compile();
    VkPipeline getPipeline();
    void cleanup();
};
} // namespace phox

#endif // PHOX_PIPELINE_HPP
