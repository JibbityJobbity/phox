#include "phox/vulkan/pipeline/Shader.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace phox;

vk::Shader::Shader(Device* device, VkShaderModule shaderModule, ShaderType type)
    : phox::Shader(type), m_device(device), m_shaderModule(shaderModule),
      m_type(type) {
}

vk::Shader::~Shader() {
    if (m_shaderModule != VK_NULL_HANDLE && m_device != nullptr &&
        m_device->m_device != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device->m_device, m_shaderModule, nullptr);
    }
    m_shaderModule = VK_NULL_HANDLE;
}

std::unique_ptr<vk::Shader> vk::Shader::fromSpirv(phox::Device& device, const uint8_t *code, const size_t length, ShaderType type) {
    assert(code != NULL && length > 0);
    vk::Device& vk_device = static_cast<vk::Device&>(device);
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = length;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code);

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vk_device.m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    return std::make_unique<vk::Shader>(&vk_device, shaderModule, type);
}

std::unique_ptr<vk::Shader> vk::Shader::fromFile(phox::Device& device, const std::string& path, ShaderType type) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }

    const std::streamsize length = in.tellg();
    if (length <= 0 || (length % 4) != 0) {
        throw std::runtime_error("Shader file is invalid SPIR-V: " + path);
    }
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> code(static_cast<size_t>(length));
    in.read(reinterpret_cast<char*>(code.data()), length);
    if (!in.good()) {
        throw std::runtime_error("Failed reading shader file: " + path);
    }

    return fromSpirv(device, code.data(), code.size(), type);
}

std::unique_ptr<vk::Shader> vk::Shader::fromGlsl(phox::Device&, const std::string&, ShaderType) {
    throw std::runtime_error(
        "Shader::fromGlsl is not implemented. Compile GLSL to SPIR-V first.");
}

VkShaderModule vk::Shader::getShaderModule() {
    return m_shaderModule;
}