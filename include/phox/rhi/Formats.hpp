#ifndef PHOX_RHI_FORMATS_HPP
#define PHOX_RHI_FORMATS_HPP

#include <cstdint>

namespace phox::rhi {

enum class Format : uint16_t {
    Undefined,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    RGBA16Float,
    RGBA32Float,
    R32Float,
    D32Float,
    D24UnormS8Uint
};
enum class TextureUsage : uint32_t {
    Sampled = 1,
    Storage = 2,
    ColorAttachment = 4,
    DepthAttachment = 8,
    TransferSrc = 16,
    TransferDst = 32
};
enum class BufferUsage : uint32_t {
    Vertex = 1,
    Index = 2,
    Uniform = 4,
    Storage = 8,
    Indirect = 16,
    TransferSrc = 32,
    TransferDst = 64
};
enum class MemoryKind : uint8_t { GpuOnly, Upload, Readback }; // Upload = CPU-write, Readback = CPU-read
enum class ShaderStage : uint8_t { Vertex, Fragment, Compute, Task, Mesh };

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr TextureUsage &operator|=(TextureUsage &a, TextureUsage b) { return a = a | b; }

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr BufferUsage &operator|=(BufferUsage &a, BufferUsage b) { return a = a | b; }

} // namespace phox::rhi

#endif // PHOX_RHI_FORMATS_HPP
