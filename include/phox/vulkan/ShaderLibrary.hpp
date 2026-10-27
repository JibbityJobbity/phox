#ifndef PHOX_VULKAN_SHADER_LIBRARY_HPP
#define PHOX_VULKAN_SHADER_LIBRARY_HPP

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace phox::vk {

class Device;
class Shader;

/// Loads compiled shaders from a directory once and keeps them for the device's lifetime.
class ShaderLibrary {
public:
    ShaderLibrary(Device &device, std::filesystem::path directory);
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    /// The shader compiled from @p name (e.g. "forward.vert" loads forward.vert.spv). The stage comes
    /// from the extension: .vert, .frag or .comp.
    /// @throws std::runtime_error if the file is missing or invalid
    const Shader &get(const std::string &name);

private:
    Device &m_device;
    std::filesystem::path m_directory;
    std::map<std::string, std::unique_ptr<Shader>> m_shaders;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_SHADER_LIBRARY_HPP
