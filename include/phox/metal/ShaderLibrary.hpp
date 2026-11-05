#ifndef PHOX_METAL_SHADER_LIBRARY_HPP
#define PHOX_METAL_SHADER_LIBRARY_HPP

#include "phox/metal/MetalCommon.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace phox::mtl {

class Device;

/// Compiles the MSL sources in a directory at runtime, once per file, and finds shader functions by the
/// names PipelineDesc uses: "<stem>.<stage>" is function "<stem>_<stage>" in "<stem>.metal", e.g.
/// "forward.vert" is forward_vert in forward.metal. `#include "file"` lines are inlined from the same
/// directory, since a library compiled from source has no include path.
class ShaderLibrary {
public:
    ShaderLibrary(Device &device, std::filesystem::path directory);
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    /// @throws std::runtime_error if the source is missing, does not compile or lacks the function
    id<MTLFunction> function(const std::string &name);

private:
    id<MTLLibrary> library(const std::string &stem);

    Device &m_device;
    std::filesystem::path m_directory;
    std::unordered_map<std::string, id<MTLLibrary>> m_libraries;
};

} // namespace phox::mtl

#endif // PHOX_METAL_SHADER_LIBRARY_HPP
