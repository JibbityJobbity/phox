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
/// Resources are pooled by the queues that use them (QueueMask). One used on a single queue can be
/// handed out again straight away, even while commands that used it are still executing: it only ever
/// goes to passes on that queue, and the graph's first use of a transient is a barrier that waits for
/// all earlier work on the queue (ALL_COMMANDS, MEMORY_WRITE) and, for images, discards the old
/// contents. One used on both queues is not ordered by that barrier, so it becomes free again only once
/// the frames that may still use it have completed (framesInFlight later). Resources that go unused for a
/// while (e.g. after a resize) are destroyed by collect().
class TransientPool {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 60;

    /// @param framesInFlight  how many frames may be executing at once
    explicit TransientPool(Device &device, std::uint64_t framesInFlight = 2);
    ~TransientPool();
    TransientPool(const TransientPool &) = delete;
    TransientPool &operator=(const TransientPool &) = delete;

    /// A free image matching @p info (ignoring the name) for passes on @p queues, created if there is none.
    /// Images for both queues are created with ImageCreateInfo::sharedQueues.
    Image &acquire(const ImageCreateInfo &info, std::uint64_t frame, QueueMask queues = queueBit(Queue::Graphics));
    /// A free device-local buffer matching @p info (ignoring the name), created if there is none.
    Buffer &acquire(const BufferCreateInfo &info, std::uint64_t frame, QueueMask queues = queueBit(Queue::Graphics));
    void release(const Image &image, std::uint64_t frame);
    void release(const Buffer &buffer, std::uint64_t frame);
    /// Frees what was released on both queues framesInFlight or more frames ago, and destroys resources
    /// unused for kEvictAfterFrames. Call at the start of a frame, after waiting for the frame that last
    /// used its frame context, so the GPU is known to have finished anything that old.
    void collect(std::uint64_t frame);

    std::size_t imageCount() const { return m_images.size(); }
    std::size_t bufferCount() const { return m_buffers.size(); }

private:
    template <class T, class Info> struct Entry {
        std::unique_ptr<T> resource;
        Info info;
        QueueMask queues = 0;
        bool inUse = false;   ///< acquired, or released on both queues and waiting for collect()
        bool pending = false; ///< released, but waiting for collect()
        std::uint64_t lastUsed = 0;
    };

    Device &m_device;
    std::uint64_t m_framesInFlight;
    std::vector<Entry<Image, ImageCreateInfo>> m_images;
    std::vector<Entry<Buffer, BufferCreateInfo>> m_buffers;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_TRANSIENT_POOL_HPP
