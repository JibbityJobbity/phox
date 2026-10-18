#ifndef PHOX_VULKAN_GRAPH_EXECUTOR_HPP
#define PHOX_VULKAN_GRAPH_EXECUTOR_HPP

#include "phox/RenderGraph.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <vector>

namespace phox {
namespace vk {

class Device;

/// Runs a RenderGraph by recording into a single command buffer. It is also the PassContext handed to
/// pass callbacks, which downcast to this type to get the command buffer and image handles.
///
/// Transient images are created on allocate() and freed when the executor is destroyed (the destructor
/// waits for the device first), because the recorded commands may still be in flight when the graph
/// "releases" them.
class GraphExecutor : public phox::GraphBackend, public phox::PassContext {
public:
    explicit GraphExecutor(Device &device);
    ~GraphExecutor() override;
    GraphExecutor(const GraphExecutor &) = delete;
    GraphExecutor &operator=(const GraphExecutor &) = delete;

    void setCommandBuffer(VkCommandBuffer commandBuffer) { m_commandBuffer = commandBuffer; }
    VkCommandBuffer commandBuffer() const { return m_commandBuffer; }
    Device &device() const { return m_device; }

    /// Registers the VkImage behind an imported graph image (e.g. the acquired swapchain image).
    void bindImported(ResourceHandle handle, VkImage image);

    VkImage image(ResourceHandle handle) const;
    /// Colour view of a graph-owned image.
    VkImageView view(ResourceHandle handle) const;

    // GraphBackend
    void allocate(const ImageInfo &image) override;
    void release(const ImageInfo &image) override;
    void transition(const ImageInfo &image, ImageUsage from, ImageUsage to) override;
    PassContext &passContext() override { return *this; }

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        bool owned = false;
    };

    Slot &slot(ResourceHandle handle);
    const Slot &slot(ResourceHandle handle) const;

    Device &m_device;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::vector<Slot> m_slots;
};

} // namespace vk
} // namespace phox

#endif // PHOX_VULKAN_GRAPH_EXECUTOR_HPP
