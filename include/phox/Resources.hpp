#ifndef PHOX_RESOURCES_HPP
#define PHOX_RESOURCES_HPP

#include <cstdint>
#include <string>

/// Backend-free descriptions of GPU resources: formats, the states a resource can be used in, and
/// what is needed to create one. The render graph and the device both speak this vocabulary.
namespace phox {

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend bool operator==(const Extent2D &, const Extent2D &) = default;
};

enum class Format : std::uint8_t {
    Undefined,
    RGBA8Unorm,
    BGRA8Unorm,
    RGBA8Srgb,
    BGRA8Srgb,
    RGBA16Float,
    R32Float,
    D32Float,
    D24UnormS8,
};

constexpr bool isDepthFormat(Format format) { return format == Format::D32Float || format == Format::D24UnormS8; }
constexpr bool hasStencil(Format format) { return format == Format::D24UnormS8; }

constexpr std::uint32_t bytesPerPixel(Format format) {
    switch (format) {
    case Format::Undefined: return 0;
    case Format::RGBA16Float: return 8;
    default: return 4;
    }
}

/// How an image is used at one point in a frame. Each usage implies a layout, pipeline stages and
/// memory accesses in the backend; the render graph inserts the barriers between them.
enum class ImageUsage : std::uint8_t {
    Undefined,       ///< contents are discarded (only as an initial state)
    ColorAttachment, ///< rendered to
    DepthAttachment, ///< depth tested and written
    DepthReadOnly,   ///< depth tested without writes, and/or sampled
    Sampled,         ///< read through a sampler in shaders
    StorageRead,     ///< read as a storage image
    StorageWrite,    ///< written (and possibly read) as a storage image
    TransferSrc,     ///< copied or blitted from
    TransferDst,     ///< copied, blitted or cleared to
    Present,         ///< handed to the presentation engine (only as a final state)
};

/// How a buffer is used at one point in a frame.
enum class BufferUsage : std::uint8_t {
    Undefined,    ///< contents are discarded (only as an initial state)
    Vertex,
    Index,
    Uniform,
    StorageRead,
    StorageWrite, ///< written (and possibly read) by shaders
    Indirect,     ///< indirect draw or dispatch arguments, which the pass's shaders may read too
    TransferSrc,
    TransferDst,
    HostRead,     ///< read by the CPU once the frame has completed (only as a final state)
    HostWrite,    ///< written by the CPU before the frame is submitted (only as an initial state)
};

constexpr std::uint32_t usageBit(ImageUsage usage) { return 1u << static_cast<std::uint32_t>(usage); }
constexpr std::uint32_t usageBit(BufferUsage usage) { return 1u << static_cast<std::uint32_t>(usage); }

/// A set of usages, as a mask of usageBit(). Resources are created with the set they will be used in.
using ImageUsageMask = std::uint32_t;
using BufferUsageMask = std::uint32_t;

enum class MemoryLocation : std::uint8_t {
    DeviceLocal, ///< GPU only
    Upload,      ///< written sequentially by the CPU, read by the GPU; persistently mapped
    Readback,    ///< written by the GPU, read by the CPU; persistently mapped
};

struct ImageCreateInfo {
    Extent2D extent;
    Format format = Format::RGBA8Unorm;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    ImageUsageMask usage = 0;
    std::string name; ///< for debuggers and validation messages
};

struct BufferCreateInfo {
    std::uint64_t size = 0;
    BufferUsageMask usage = 0;
    MemoryLocation memory = MemoryLocation::DeviceLocal;
    std::string name; ///< for debuggers and validation messages
    /// Shaders may read the buffer through its address (Vulkan: Buffer::deviceAddress) instead of a descriptor.
    bool deviceAddress = false;
};

} // namespace phox

#endif // PHOX_RESOURCES_HPP
