#include "phox/vulkan/ShaderLibrary.hpp"

#include "phox/vulkan/pipeline/Shader.hpp"

#include <stdexcept>

namespace phox::vk {

ShaderLibrary::ShaderLibrary(Device &device, std::filesystem::path directory)
    : m_device(device), m_directory(std::move(directory)) {}

ShaderLibrary::~ShaderLibrary() = default;

const Shader &ShaderLibrary::get(const std::string &name) {
    if (auto it = m_shaders.find(name); it != m_shaders.end()) {
        return *it->second;
    }
    const std::string extension = std::filesystem::path(name).extension().string();
    ShaderType type;
    if (extension == ".vert") {
        type = ShaderType::VERTEX;
    } else if (extension == ".frag") {
        type = ShaderType::FRAGMENT;
    } else if (extension == ".comp") {
        type = ShaderType::COMPUTE;
    } else {
        throw std::runtime_error("shader '" + name + "' needs a .vert, .frag or .comp extension");
    }
    auto shader = Shader::fromFile(m_device, (m_directory / (name + ".spv")).string(), type);
    return *m_shaders.emplace(name, std::move(shader)).first->second;
}

} // namespace phox::vk
