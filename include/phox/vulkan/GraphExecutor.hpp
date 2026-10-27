#ifndef PHOX_VULKAN_GRAPH_EXECUTOR_HPP
#define PHOX_VULKAN_GRAPH_EXECUTOR_HPP

#include "phox/RenderGraph.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <vector>

namespace phox::vk {

class Device;
class Image;
class TransientPool;

/// Runs a RenderGraph by recording into one command buffer. It is also the PassContext handed to pass
/// callbacks, which downcast to this type to get the command buffer and image handles.
///
/// Transient images come from a TransientPool and go back to it when the graph releases them. The
/// executor owns nothing across frames, so it is created per frame.
class GraphExecutor : public phox::GraphBackend, public phox::PassContext {
public:
    GraphExecutor(Device &device, TransientPool &pool, std::uint64_t frame, VkCommandBuffer commandBuffer);
    /// Returns images still held, e.g. after an exception while recording.
    ~GraphExecutor() override;
    GraphExecutor(const GraphExecutor &) = delete;
    GraphExecutor &operator=(const GraphExecutor &) = delete;

    VkCommandBuffer commandBuffer() const { return m_commandBuffer; }
    Device &device() const { return m_device; }

    /// Registers the VkImage behind an imported graph image (e.g. the acquired swapchain image).
    void bindImported(ResourceHandle handle, VkImage image, VkImageView view = VK_NULL_HANDLE);

    VkImage image(ResourceHandle handle) const;
    VkImageView view(ResourceHandle handle) const;

    // GraphBackend
    void allocate(const ImageInfo &image) override;
    void release(const ImageInfo &image) override;
    void transition(const ImageInfo &image, ImageUsage from, ImageUsage to) override;
    PassContext &passContext() override { return *this; }

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        Image *pooled = nullptr; ///< set while a transient is held from the pool
    };

    Slot &slot(ResourceHandle handle);
    const Slot &slot(ResourceHandle handle) const;

    Device &m_device;
    TransientPool &m_pool;
    std::uint64_t m_frame;
    VkCommandBuffer m_commandBuffer;
    std::vector<Slot> m_slots;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_GRAPH_EXECUTOR_HPP
