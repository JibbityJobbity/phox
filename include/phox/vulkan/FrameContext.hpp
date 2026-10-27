#ifndef PHOX_VULKAN_FRAME_CONTEXT_HPP
#define PHOX_VULKAN_FRAME_CONTEXT_HPP

#include "phox/vulkan/DescriptorAllocator.hpp"
#include "phox/vulkan/UploadArena.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

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

/// Everything one frame in flight needs: its command buffer, the fence that says the GPU is done with
/// it, the semaphore for acquiring a swapchain image, and per-frame memory and descriptors.
/// The renderer cycles through a small ring of these; begin() makes one ready for reuse.
class FrameContext {
public:
    FrameContext(Device &device, std::uint32_t index);
    ~FrameContext();
    FrameContext(const FrameContext &) = delete;
    FrameContext &operator=(const FrameContext &) = delete;

    /// Waits for this context's previous frame to finish on the GPU, then recycles its resources.
    void begin();

    VkCommandBuffer commandBuffer() const { return m_commandBuffer; }
    /// Signalled when the frame's submission completes. The renderer resets it right before submitting.
    VkFence fence() const { return m_fence; }
    VkSemaphore imageAvailable() const { return m_imageAvailable; }
    UploadArena &arena() { return m_arena; }
    DescriptorAllocator &descriptors() { return m_descriptors; }
    DeletionQueue &deferred() { return m_deferred; }

private:
    Device &m_device;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    UploadArena m_arena;
    DescriptorAllocator m_descriptors;
    DeletionQueue m_deferred;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_FRAME_CONTEXT_HPP
