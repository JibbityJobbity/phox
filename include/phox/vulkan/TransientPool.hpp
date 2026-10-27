#ifndef PHOX_VULKAN_TRANSIENT_POOL_HPP
#define PHOX_VULKAN_TRANSIENT_POOL_HPP

#include "phox/Resources.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace phox::vk {

class Device;
class Image;

/// Recycles the render graph's transient images across passes and frames instead of creating and
/// destroying them every frame.
///
/// A released image can be handed out again straight away, even while commands that used it are still
/// executing: every frame is submitted to the same queue, and the graph's first use of a transient
/// image waits for all earlier work on it (a barrier from ALL_COMMANDS that discards the contents).
/// Images that go unused for a while (e.g. after a resize) are destroyed by collect().
class TransientPool {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 60;

    explicit TransientPool(Device &device);
    ~TransientPool();
    TransientPool(const TransientPool &) = delete;
    TransientPool &operator=(const TransientPool &) = delete;

    /// A free image matching @p info (ignoring the name), created if there is none.
    Image &acquire(const ImageCreateInfo &info, std::uint64_t frame);
    void release(const Image &image, std::uint64_t frame);
    /// Destroys images unused for kEvictAfterFrames. Call at the start of a frame, after its fence wait,
    /// so the GPU is known to have finished anything that old.
    void collect(std::uint64_t frame);

    std::size_t imageCount() const { return m_images.size(); }

private:
    struct Entry {
        std::unique_ptr<Image> image;
        bool inUse = false;
        std::uint64_t lastUsed = 0;
    };

    Device &m_device;
    std::vector<Entry> m_images;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_TRANSIENT_POOL_HPP
