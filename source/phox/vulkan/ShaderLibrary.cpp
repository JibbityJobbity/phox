#include "phox/vulkan/ShaderLibrary.hpp"

#include "phox/vulkan/pipeline/Shader.hpp"

#include <algorithm>
#include <stdexcept>

namespace phox::vk {

ShaderLibrary::ShaderLibrary(Device &device, std::filesystem::path directory)
    : m_device(device), m_directories{std::move(directory)} {}

void ShaderLibrary::addDirectory(std::filesystem::path directory) {
    if (std::find(m_directories.begin(), m_directories.end(), directory) == m_directories.end()) {
        m_directories.push_back(std::move(directory));
    }
}

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
    } else if (extension == ".task") {
        type = ShaderType::TASK;
    } else if (extension == ".mesh") {
        type = ShaderType::MESH;
    } else {
        throw std::runtime_error("shader '" + name + "' needs a .vert, .frag, .comp, .task or .mesh extension");
    }
    std::filesystem::path path = m_directories.front() / (name + ".spv");
    for (const std::filesystem::path &directory : m_directories) {
        if (std::filesystem::exists(directory / (name + ".spv"))) {
            path = directory / (name + ".spv");
            break;
        }
    }
    auto shader = Shader::fromFile(m_device, path.string(), type);
    return *m_shaders.emplace(name, std::move(shader)).first->second;
}

} // namespace phox::vk
