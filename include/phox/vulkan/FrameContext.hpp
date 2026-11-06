#ifndef PHOX_VULKAN_FRAME_CONTEXT_HPP
#define PHOX_VULKAN_FRAME_CONTEXT_HPP

#include "phox/vulkan/DescriptorAllocator.hpp"
#include "phox/vulkan/PassTimer.hpp"
#include "phox/vulkan/UploadArena.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace phox::vk {

class Device;

/// Work to run once the GPU has finished a frame, e.g. destroying objects its commands still use.
class DeletionQueue {
public:
    DeletionQueue() = default;
    ~DeletionQueue() { flush(); }
    DeletionQueue(const DeletionQueue &) = delete;
    DeletionQueue &operator=(const DeletionQueue &) = delete;

    void push(std::function<void()> fn) { m_entries.push_back(std::move(fn)); }
    /// Runs the entries newest first.
    void flush() {
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) (*it)();
        m_entries.clear();
    }

private:
    std::vector<std::function<void()>> m_entries;
};

/// Everything one frame in flight needs: its command buffers, what says the GPU is done with it (a fence
/// on its last graphics submission, and the async compute timeline value of its last compute one), the
/// semaphore for acquiring a swapchain image, per-frame memory and descriptors, and GPU timers.
/// The renderer cycles through a small ring of these; begin() makes one ready for reuse.
class FrameContext {
public:
    FrameContext(Device &device, std::uint32_t index);
    ~FrameContext();
    FrameContext(const FrameContext &) = delete;
    FrameContext &operator=(const FrameContext &) = delete;

    /// Waits for this context's previous frame to finish on the GPU, collects its pass timings, then
    /// recycles its resources.
    void begin();

    /// The frame's first graphics command buffer, for the work recorded before its render graph.
    VkCommandBuffer commandBuffer() const { return m_commandBuffer; }
    /// Another command buffer for @p queue, not yet begun. Valid until the next begin().
    VkCommandBuffer extraCommandBuffer(Queue queue);
    /// begin() also waits for the async compute timeline to reach @p value (the frame's last submission there).
    void setComputeWait(std::uint64_t value) { m_computeWait = value; }
    /// Signalled when the frame's submission completes. The renderer resets it right before submitting.
    VkFence fence() const { return m_fence; }
    VkSemaphore imageAvailable() const { return m_imageAvailable; }
    UploadArena &arena() { return m_arena; }
    DescriptorAllocator &descriptors() { return m_descriptors; }
    DeletionQueue &deferred() { return m_deferred; }
    PassTimer &timer() { return m_timer; }

private:
    Device &m_device;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandPool m_computePool = VK_NULL_HANDLE; ///< with async compute
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    struct Extra {
        std::vector<VkCommandBuffer> buffers; ///< allocated so far, reused frame after frame
        std::size_t used = 0;
    };
    std::array<Extra, 2> m_extra; ///< indexed by Queue
    std::uint64_t m_computeWait = 0;
    std::uint32_t m_index = 0;
    VkFence m_fence = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    UploadArena m_arena;
    DescriptorAllocator m_descriptors;
    DeletionQueue m_deferred;
    PassTimer m_timer;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_FRAME_CONTEXT_HPP
