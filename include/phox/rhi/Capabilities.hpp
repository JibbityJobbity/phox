#ifndef PHOX_RHI_CAPABILITIES_HPP
#define PHOX_RHI_CAPABILITIES_HPP

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace phox::rhi {

enum class Feature : uint32_t {
    MeshShader,
    RayTracing,
    BindlessDescriptors,
    DrawIndirectCount,
    ShaderFloat16,
    Atomic64,
    VariableRateShading
};

struct Capabilities {
    std::string deviceName;
    uint32_t apiMajor = 0, apiMinor = 0;
    std::bitset<32> features; // indexed by Feature
    uint32_t maxTextureSize2D = 0;
    uint32_t maxPushConstantBytes = 0;
    bool has(Feature f) const { return features.test(static_cast<size_t>(f)); }
};

constexpr std::string_view toString(Feature f) {
    switch (f) {
    case Feature::MeshShader: return "MeshShader";
    case Feature::RayTracing: return "RayTracing";
    case Feature::BindlessDescriptors: return "BindlessDescriptors";
    case Feature::DrawIndirectCount: return "DrawIndirectCount";
    case Feature::ShaderFloat16: return "ShaderFloat16";
    case Feature::Atomic64: return "Atomic64";
    case Feature::VariableRateShading: return "VariableRateShading";
    }
    return "Unknown";
}

} // namespace phox::rhi

#endif // PHOX_RHI_CAPABILITIES_HPP
