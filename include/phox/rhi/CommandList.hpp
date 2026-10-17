#ifndef PHOX_RHI_COMMAND_LIST_HPP
#define PHOX_RHI_COMMAND_LIST_HPP

#include "phox/rhi/Formats.hpp"
#include "phox/rhi/Handles.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace phox::rhi {

enum class ResourceState : uint8_t {
    Undefined,
    ColorAttachment,
    DepthWrite,
    DepthRead,
    ShaderRead,
    ShaderWrite,
    TransferSrc,
    TransferDst,
    VertexOrIndex,
    Uniform,
    Present
};
struct TextureBarrier {
    TextureHandle texture;
    ResourceState from, to;
};
struct BufferBarrier {
    BufferHandle buffer;
    ResourceState from, to;
};
struct ColorTarget {
    TextureHandle texture;
    bool clear = true;
    std::array<float, 4> clearColor{0, 0, 0, 1};
};
struct DepthTarget {
    TextureHandle texture;
    bool clear = true;
    float clearDepth = 1.0f;
};
struct RenderingInfo {
    std::vector<ColorTarget> colors;
    std::optional<DepthTarget> depth;
    uint32_t width, height;
};

class CommandList {
  public:
    virtual ~CommandList() = default;
    virtual void barrier(std::span<const TextureBarrier>, std::span<const BufferBarrier>) = 0;
    virtual void beginRendering(const RenderingInfo &) = 0; // must: sets viewport + scissor to width x height
    virtual void endRendering() = 0;
    virtual void bindPipeline(PipelineHandle) = 0;
    virtual void bindVertexBuffer(BufferHandle, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle, uint64_t offset = 0) = 0; // uint32 indices only
    virtual void bindUniformBuffer(uint32_t set, uint32_t binding, BufferHandle) = 0;
    virtual void bindStorageBuffer(uint32_t set, uint32_t binding, BufferHandle) = 0;
    virtual void bindSampledTexture(uint32_t set, uint32_t binding, TextureHandle) = 0;
    virtual void pushConstants(ShaderStage, uint32_t offset, std::span<const std::byte>) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0) = 0;
    virtual void drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) = 0; // must: assert caps.has(MeshShader)
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void copyBuffer(BufferHandle src, BufferHandle dst, uint64_t size) = 0;
    virtual void copyTextureToBuffer(TextureHandle src, BufferHandle dst) = 0; // tightly packed, mip 0
    virtual void beginDebugLabel(const char *name) = 0;
    virtual void endDebugLabel() = 0;
};

} // namespace phox::rhi

#endif // PHOX_RHI_COMMAND_LIST_HPP
