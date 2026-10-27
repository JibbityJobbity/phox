#ifndef PHOX_VULKAN_SHADER_HPP
#define PHOX_VULKAN_SHADER_HPP

#include "phox/pipeline/Shader.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace phox::vk {

class Device;

/// A SPIR-V shader module.
class Shader final : public phox::Shader {
public:
    Shader(Device &device, VkShaderModule module, ShaderType type);
    ~Shader() override;

    VkShaderModule module() const { return m_module; }

    /// @throws std::runtime_error if the file is missing or not SPIR-V
    static std::unique_ptr<Shader> fromFile(Device &device, const std::string &path, ShaderType type);
    static std::unique_ptr<Shader> fromSpirv(Device &device, const std::uint8_t *code, std::size_t length,
                                             ShaderType type);

private:
    Device &m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_HPP
