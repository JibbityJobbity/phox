#ifndef PHOX_SHADER_HPP
#define PHOX_SHADER_HPP

#include "../Common.hpp"

namespace phox {
class Shader {
  private:
    VkDevice m_device;
    VkShaderModule m_shader;

  public:
    Shader(VkDevice device, VkShaderModule shader);
    static Shader fromFile(const char *path);
    VkShaderModule getShader();
    void initialize();
    void cleanup();
};
} // namespace phox

#endif // PHOX_SHADER_HPP