#ifndef PHOX_VULKAN_TRANSIENT_POOL_HPP
#define PHOX_VULKAN_TRANSIENT_POOL_HPP

#include "phox/Resources.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace phox::vk {

class Buffer;
class Device;
class Image;

/// Recycles the render graph's transient images and buffers across passes and frames instead of
/// creating and destroying them every frame.
///
/// A released resource can be handed out again straight away, even while commands that used it are
/// still executing: every frame is submitted to the same queue, and the graph's first use of a
/// transient is a barrier that waits for all earlier work (ALL_COMMANDS, MEMORY_WRITE) and, for
/// images, discards the old contents. Resources that go unused for a while (e.g. after a resize) are
/// destroyed by collect().
class TransientPool {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 60;

    explicit TransientPool(Device &device);
    ~TransientPool();
    TransientPool(const TransientPool &) = delete;
    TransientPool &operator=(const TransientPool &) = delete;

    /// A free image matching @p info (ignoring the name), created if there is none.
    Image &acquire(const ImageCreateInfo &info, std::uint64_t frame);
    /// A free device-local buffer matching @p info (ignoring the name), created if there is none.
    Buffer &acquire(const BufferCreateInfo &info, std::uint64_t frame);
    void release(const Image &image, std::uint64_t frame);
    void release(const Buffer &buffer, std::uint64_t frame);
    /// Destroys resources unused for kEvictAfterFrames. Call at the start of a frame, after its fence wait,
    /// so the GPU is known to have finished anything that old.
    void collect(std::uint64_t frame);

    std::size_t imageCount() const { return m_images.size(); }
    std::size_t bufferCount() const { return m_buffers.size(); }

private:
    template <class T, class Info> struct Entry {
        std::unique_ptr<T> resource;
        Info info;
        bool inUse = false;
        std::uint64_t lastUsed = 0;
    };

    Device &m_device;
    std::vector<Entry<Image, ImageCreateInfo>> m_images;
    std::vector<Entry<Buffer, BufferCreateInfo>> m_buffers;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_TRANSIENT_POOL_HPP
