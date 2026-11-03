#ifndef PHOX_VULKAN_MESHLET_VISIBILITY_HPP
#define PHOX_VULKAN_MESHLET_VISIBILITY_HPP

#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace phox {
struct RenderView;
} // namespace phox

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;
class MeshCache;

/// For two-phase occlusion culling: whether each meshlet of each drawn object was visible at the end of
/// the previous frame, one bit per meshlet in a storage buffer. The early forward pass draws what was
/// visible; the late pass rewrites the bits.
///
/// The bits are a hint. Wrong ones cost time, never correctness, since the late pass tests every meshlet
/// the early pass skipped. So the bookkeeping stays simple: objects (by Object::id) get ranges of whole
/// 32-bit words, one per task shader workgroup, from a bump allocator, and when the buffer is full it
/// starts over, growing if needed, with every bit cleared.
class MeshletVisibility {
public:
    static constexpr std::uint32_t kInitialWords = 1u << 14;

    struct Frame {
        std::vector<std::uint32_t> offsets; ///< per draw of the view: the first word of its bits
        bool clear = false;                 ///< the buffer's contents are undefined: clear it before use
    };

    explicit MeshletVisibility(Device &device);
    ~MeshletVisibility();
    MeshletVisibility(const MeshletVisibility &) = delete;
    MeshletVisibility &operator=(const MeshletVisibility &) = delete;

    /// Assigns each draw of @p view its words; objects new to the store, or whose mesh changed, get fresh
    /// ones. A replaced buffer is destroyed once @p frame has finished. Call after MeshCache::prepare.
    Frame prepare(const RenderView &view, const MeshCache &meshes, FrameContext &frame);
    Buffer &buffer() const { return *m_buffer; }

private:
    struct Range {
        std::uint32_t offset = 0;
        std::uint32_t words = 0;
        std::uint64_t mesh = 0;
        std::uint64_t meshVersion = 0;
    };

    Device &m_device;
    std::unique_ptr<Buffer> m_buffer;
    std::uint32_t m_used = 0; ///< words handed out since the buffer was last cleared
    std::unordered_map<std::uint64_t, Range> m_ranges;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_MESHLET_VISIBILITY_HPP
