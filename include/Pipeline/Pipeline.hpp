#ifndef PHOX_PIPELINE_HPP
#define PHOX_PIPELINE_HPP

#include "../Common.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "Device.hpp"
#include "Shader.hpp"

namespace phox {
class Pipeline {
  private:
  protected:
    Pipeline() = default;
    ~Pipeline() = default;
    VkDevice m_device;

  public:
    Pipeline(VkDevice device);
    virtual void addShader(Shader shader, VkShaderStageFlagBits stage);
    virtual void compile();
    VkPipeline getPipeline();
    void cleanup();
};
} // namespace phox

#endif // PHOX_PIPELINE_HPP
