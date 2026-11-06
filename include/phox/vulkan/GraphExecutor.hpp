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
    Device &device() const;
    /// Per-frame memory (UploadArena), descriptor sets and deferred deletion.
    FrameContext &frame() const;

private:
    friend class GraphExecutor;
    explicit PassContext(GraphExecutor &executor) : m_executor(executor) {}
    GraphExecutor &m_executor;
};

/// What submit() adds to the graph's own submissions: the swapchain's semaphores and the frame's fence.
struct FrameSubmit {
    VkSemaphore waitSemaphore = VK_NULL_HANDLE; ///< binary; waited for by the submission that first uses @p waitImage
    ImageHandle waitImage;                      ///< e.g. the acquired swapchain image
    VkSemaphore signalSemaphore = VK_NULL_HANDLE; ///< binary; signalled by the last graphics submission
    VkFence fence = VK_NULL_HANDLE;               ///< signalled by the last graphics submission
};

/// Executes a RenderGraph: one command buffer per submission (the first graphics one is the frame's own,
/// already begun, holding the work recorded before the graph), synchronization2 barriers batched per
/// pass, dynamic rendering for graphics passes, transient images and buffers from a TransientPool, and a
/// GPU timestamp pair around each pass (the frame's PassTimer). submit() then submits them in order, each
/// signalling its queue's timeline semaphore and waiting on the other's where the graph says. Created
/// per frame; it owns nothing across frames.
class GraphExecutor final : public phox::GraphBackend {
public:
    /// @param commandBuffer  the frame's first graphics command buffer, begun
    GraphExecutor(const RenderGraph &graph, Device &device, TransientPool &pool, FrameContext &frame,
                  std::uint64_t frameNumber, VkCommandBuffer commandBuffer);
    /// Returns pooled resources still held, e.g. after an exception while recording.
    ~GraphExecutor() override;
    GraphExecutor(const GraphExecutor &) = delete;
    GraphExecutor &operator=(const GraphExecutor &) = delete;

    /// Supplies the Vulkan objects behind an imported image. @p view is needed only if a pass binds the
    /// image as an attachment or asks for its view.
    void bindImage(ImageHandle handle, VkImage image, VkImageView view = VK_NULL_HANDLE);
    void bindBuffer(BufferHandle handle, VkBuffer buffer);

    /// Ends the command buffers and submits the executed graph. Returns the async compute timeline value
    /// of the frame's last compute submission (0 if none), which the frame must reach before it completes.
    /// @throws std::runtime_error if a submission fails
    std::uint64_t submit(const FrameSubmit &extra);

    // GraphBackend
    void beginSubmission(const Submission &submission, std::uint32_t index) override;
    void endSubmission(const Submission &submission, std::uint32_t index) override;
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
        Buffer *pooled = nullptr;
    };

    ImageSlot &imageSlot(std::uint32_t index);
    const ImageSlot &boundImage(std::uint32_t index) const;
    BufferSlot &bufferSlot(std::uint32_t index);
    const BufferSlot &boundBuffer(std::uint32_t index) const;

    struct Recorded {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        bool commands = false;                               ///< holds passes or barriers
        VkPipelineStageFlags2 waitStages = VK_PIPELINE_STAGE_2_NONE; ///< what crossQueue barriers need waited for
    };

    const RenderGraph &m_graph;
    Device &m_device;
    TransientPool &m_pool;
    FrameContext &m_frame;
    std::uint64_t m_frameNumber;
    VkCommandBuffer m_frameCommandBuffer;
    VkCommandBuffer m_commandBuffer; ///< the current submission's
    Queue m_queue = Queue::Graphics; ///< the current submission's
    std::uint32_t m_submission = 0;
    std::vector<Recorded> m_recorded; ///< per submission
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
