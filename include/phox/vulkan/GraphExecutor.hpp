#ifndef PHOX_VULKAN_GRAPH_EXECUTOR_HPP
#define PHOX_VULKAN_GRAPH_EXECUTOR_HPP

#include "phox/RenderGraph.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <vector>

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;
class GraphExecutor;
class Image;
class TransientPool;

/// What a pass body receives: the command buffer, and the Vulkan objects behind the graph's handles.
class PassContext final : public phox::PassContext {
public:
    /// Pass callbacks get a phox::PassContext; this is the Vulkan one behind it.
    static PassContext &from(phox::PassContext &context) { return static_cast<PassContext &>(context); }

    VkCommandBuffer cmd() const;
    VkImage image(ImageHandle handle) const;
    VkImageView view(ImageHandle handle) const;
    /// A view of one mip of a transient image, e.g. to write it as a storage image.
    VkImageView mipView(ImageHandle handle, std::uint32_t level) const;
    Extent2D extent(ImageHandle handle) const;
    VkBuffer buffer(BufferHandle handle) const;
    /// The device address of a buffer created with BufferDesc::deviceAddress (or imported with one).
    VkDeviceAddress bufferAddress(BufferHandle handle) const;
    Device &device() const;
    /// Per-frame memory (UploadArena), descriptor sets and deferred deletion.
    FrameContext &frame() const;

private:
    friend class GraphExecutor;
    explicit PassContext(GraphExecutor &executor) : m_executor(executor) {}
    GraphExecutor &m_executor;
};

/// Executes a RenderGraph into one command buffer: synchronization2 barriers batched per pass, dynamic
/// rendering for graphics passes, transient images and buffers from a TransientPool, and a GPU timestamp
/// pair around each pass (the frame's PassTimer). Created per frame; it owns nothing across frames.
class GraphExecutor final : public phox::GraphBackend {
public:
    GraphExecutor(const RenderGraph &graph, Device &device, TransientPool &pool, FrameContext &frame,
                  std::uint64_t frameNumber, VkCommandBuffer commandBuffer);
    /// Returns pooled resources still held, e.g. after an exception while recording.
    ~GraphExecutor() override;
    GraphExecutor(const GraphExecutor &) = delete;
    GraphExecutor &operator=(const GraphExecutor &) = delete;

    /// Supplies the Vulkan objects behind an imported image. @p view is needed only if a pass binds the
    /// image as an attachment or asks for its view.
    void bindImage(ImageHandle handle, VkImage image, VkImageView view = VK_NULL_HANDLE);
    void bindBuffer(BufferHandle handle, VkBuffer buffer, VkDeviceAddress address = 0);

    // GraphBackend
    void acquire(const ImageInfo &image) override;
    void acquire(const BufferInfo &buffer) override;
    void release(const ImageInfo &image) override;
    void release(const BufferInfo &buffer) override;
    void barriers(const BarrierBatch &batch) override;
    void beginPass(const CompiledPass &pass) override;
    void endPass(const CompiledPass &pass) override;
    phox::PassContext &passContext() override { return m_context; }

private:
    friend class PassContext;

    struct ImageSlot {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        Image *pooled = nullptr; ///< held from the pool until released
        Image *owner = nullptr;  ///< the pooled image, kept after release for passes that ran before it
    };
    struct BufferSlot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        Buffer *pooled = nullptr;
    };

    ImageSlot &imageSlot(std::uint32_t index);
    const ImageSlot &boundImage(std::uint32_t index) const;
    BufferSlot &bufferSlot(std::uint32_t index);
    const BufferSlot &boundBuffer(std::uint32_t index) const;

    const RenderGraph &m_graph;
    Device &m_device;
    TransientPool &m_pool;
    FrameContext &m_frame;
    std::uint64_t m_frameNumber;
    VkCommandBuffer m_commandBuffer;
    PassContext m_context;
    std::vector<ImageSlot> m_images;
    std::vector<BufferSlot> m_buffers;
};

/// The pipeline stage at which the compiled graph first touches @p image: the stage a submission must
/// block when the image arrives through a semaphore (e.g. an acquired swapchain image). @p meshShaders:
/// the device has task and mesh shaders, whose stages graphics passes may use.
VkPipelineStageFlags2 firstUseStage(const RenderGraph &graph, ImageHandle image, bool meshShaders);

} // namespace phox::vk

#endif // PHOX_VULKAN_GRAPH_EXECUTOR_HPP
