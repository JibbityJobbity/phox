#ifndef PHOX_VULKAN_SHADER_LIBRARY_HPP
#define PHOX_VULKAN_SHADER_LIBRARY_HPP

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace phox::vk {

class Device;
class Shader;

/// Loads compiled shaders from a directory (and any added with addDirectory, searched after it) once and
/// keeps them for the device's lifetime.
class ShaderLibrary {
public:
    ShaderLibrary(Device &device, std::filesystem::path directory);
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    /// Also look for shaders in @p directory, e.g. an application's own.
    void addDirectory(std::filesystem::path directory);

    /// The shader compiled from @p name (e.g. "forward.vert" loads forward.vert.spv), from the first
    /// directory that has it. The stage comes from the extension: .vert, .frag, .comp, .task or .mesh.
    /// @throws std::runtime_error if the file is missing or invalid
    const Shader &get(const std::string &name);

private:
    Device &m_device;
    std::vector<std::filesystem::path> m_directories;
    std::map<std::string, std::unique_ptr<Shader>> m_shaders;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_LIBRARY_HPP
