#ifndef PHOX_VULKAN_SHADER_HPP
#define PHOX_VULKAN_SHADER_HPP

#include "phox/Common.hpp"
#include "phox/Buffer.hpp"
#include "phox/pipeline/Shader.hpp"
#include "phox/vulkan/VulkanCommon.hpp"
#include "phox/vulkan/Device.hpp"

namespace phox {

namespace vk {

class Shader : public phox::Shader {
    private:
        Device* m_device = nullptr;
        VkShaderModule m_shaderModule = VK_NULL_HANDLE;
        ShaderType m_type;

    public:
        Shader(Device* device, VkShaderModule shaderModule, ShaderType type);

        ~Shader();

        VkShaderModule getShaderModule();
        static std::unique_ptr<Shader> fromFile(phox::Device& device, const std::string& path, ShaderType type);
        static std::unique_ptr<Shader> fromSpirv(phox::Device& device, const uint8_t* code, const size_t length, ShaderType type);
        static std::unique_ptr<Shader> fromGlsl(phox::Device& device, const std::string& source, ShaderType type);
};

}

}

#endif // PHOX_VULKAN_SHADER_HPP
