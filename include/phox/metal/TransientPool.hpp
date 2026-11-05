#ifndef PHOX_METAL_TRANSIENT_POOL_HPP
#define PHOX_METAL_TRANSIENT_POOL_HPP

#include "phox/Resources.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace phox::mtl {

class Buffer;
class Device;
class Image;

/// Recycles the render graph's transient textures and buffers across passes and frames.
///
/// A released resource can be handed out again straight away: Phox's Metal resources use automatic
/// hazard tracking, so Metal orders a new use after earlier commands that touched the resource. Command
/// buffers also keep the resources they reference alive, so collect() can destroy idle entries without
/// waiting for the GPU.
class TransientPool {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 60;

    explicit TransientPool(Device &device);
    ~TransientPool();
    TransientPool(const TransientPool &) = delete;
    TransientPool &operator=(const TransientPool &) = delete;

    /// A free texture matching @p info (ignoring the name), created if there is none.
    Image &acquire(const ImageCreateInfo &info, std::uint64_t frame);
    /// A free GPU-private buffer matching @p info (ignoring the name), created if there is none.
    Buffer &acquire(const BufferCreateInfo &info, std::uint64_t frame);
    void release(const Image &image, std::uint64_t frame);
    void release(const Buffer &buffer, std::uint64_t frame);
    /// Destroys resources unused for kEvictAfterFrames.
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

} // namespace phox::mtl

#endif // PHOX_METAL_TRANSIENT_POOL_HPP
