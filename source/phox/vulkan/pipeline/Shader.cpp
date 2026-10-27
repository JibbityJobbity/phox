#include "phox/vulkan/pipeline/Shader.hpp"

#include "phox/vulkan/Device.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace phox::vk {

Shader::Shader(Device &device, VkShaderModule module, ShaderType stage)
    : phox::Shader(stage), m_device(device), m_module(module) {}

Shader::~Shader() { m_device.table().vkDestroyShaderModule(m_device.handle(), m_module, nullptr); }

std::unique_ptr<Shader> Shader::fromSpirv(Device &device, const std::uint8_t *code, std::size_t length,
                                          ShaderType type) {
    if (code == nullptr || length == 0 || (length % 4) != 0) {
        throw std::runtime_error("invalid SPIR-V blob");
    }
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = length;
    createInfo.pCode = reinterpret_cast<const std::uint32_t *>(code);

    VkShaderModule module = VK_NULL_HANDLE;
    if (device.table().vkCreateShaderModule(device.handle(), &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }
    return std::make_unique<Shader>(device, module, type);
}

std::unique_ptr<Shader> Shader::fromFile(Device &device, const std::string &path, ShaderType type) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }
    const std::streamsize length = in.tellg();
    if (length <= 0 || (length % 4) != 0) {
        throw std::runtime_error("shader file is not valid SPIR-V: " + path);
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> code(static_cast<std::size_t>(length));
    in.read(reinterpret_cast<char *>(code.data()), length);
    if (!in.good()) {
        throw std::runtime_error("failed reading shader file: " + path);
    }
    auto shader = fromSpirv(device, code.data(), code.size(), type);
    device.setObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<std::uint64_t>(shader->module()), path);
    return shader;
}

} // namespace phox::vk
