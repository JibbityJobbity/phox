#ifndef PHOX_RHI_DESCS_HPP
#define PHOX_RHI_DESCS_HPP

#include "phox/rhi/Formats.hpp"
#include "phox/rhi/Handles.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace phox::rhi {

struct BufferDesc {
    uint64_t size;
    BufferUsage usage;
    MemoryKind memory;
    const char *debugName = nullptr;
};
struct TextureDesc {
    uint32_t width, height;
    uint32_t mips = 1;
    Format format;
    TextureUsage usage;
    const char *debugName = nullptr;
};
struct ShaderDesc {
    ShaderStage stage;
    std::span<const uint32_t> spirv;
    const char *entry = "main";
};

enum class VertexFormat : uint8_t { Float32, Float32x2, Float32x3, Float32x4, UInt32 };
struct VertexAttribute {
    uint32_t location;
    VertexFormat format;
    uint32_t offset;
};
struct VertexLayout {
    uint32_t stride;
    std::vector<VertexAttribute> attributes;
};

enum class Topology : uint8_t { TriangleList, LineList, PointList };
enum class CullMode : uint8_t { None, Back, Front };
struct PushConstantRange {
    ShaderStage stage;
    uint32_t offset, size;
};
enum class BindingType : uint8_t { UniformBuffer, StorageBuffer, SampledTexture, StorageTexture };
struct Binding {
    uint32_t set, binding;
    BindingType type;
    ShaderStage stage;
};

struct GraphicsPipelineDesc {
    std::vector<ShaderHandle> shaders; // must: exactly one Vertex + one Fragment, or Task?/Mesh + Fragment
    VertexLayout vertexLayout;         // ignored for mesh pipelines
    Topology topology = Topology::TriangleList;
    CullMode cull = CullMode::None;
    std::vector<Format> colorFormats; // dynamic rendering: formats, not a render pass
    Format depthFormat = Format::Undefined;
    std::vector<Binding> bindings;
    std::vector<PushConstantRange> pushConstants;
    const char *debugName = nullptr;
};
struct ComputePipelineDesc {
    ShaderHandle shader;
    std::vector<Binding> bindings;
    std::vector<PushConstantRange> pushConstants;
    const char *debugName = nullptr;
};

} // namespace phox::rhi

#endif // PHOX_RHI_DESCS_HPP
