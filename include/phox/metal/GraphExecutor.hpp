#ifndef PHOX_METAL_GRAPH_EXECUTOR_HPP
#define PHOX_METAL_GRAPH_EXECUTOR_HPP

#include "phox/RenderGraph.hpp"
#include "phox/metal/MetalCommon.hpp"

#include <cstdint>
#include <vector>

namespace phox::mtl {

class Buffer;
class Device;
class GraphExecutor;
class Image;
class TransientPool;

/// What a pass body receives: the encoder for its pass type, and the Metal objects behind the graph's
/// handles.
class PassContext final : public phox::PassContext {
public:
    /// Pass callbacks get a phox::PassContext; this is the Metal one behind it.
    static PassContext &from(phox::PassContext &context) { return static_cast<PassContext &>(context); }

    id<MTLCommandBuffer> commandBuffer() const;
    /// The encoder of the running pass, by pass type. @throws std::logic_error for another pass type
    id<MTLRenderCommandEncoder> renderEncoder() const;   ///< PassType::Graphics
    id<MTLComputeCommandEncoder> computeEncoder() const; ///< PassType::Compute
    id<MTLBlitCommandEncoder> blitEncoder() const;       ///< PassType::Transfer
    id<MTLTexture> texture(ImageHandle handle) const;
    Extent2D extent(ImageHandle handle) const;
    id<MTLBuffer> buffer(BufferHandle handle) const;
    Device &device() const;

private:
    friend class GraphExecutor;
    explicit PassContext(GraphExecutor &executor) : m_executor(executor) {}
    GraphExecutor &m_executor;
};

/// Executes a RenderGraph into one command buffer: one encoder per pass (render, compute or blit by pass
/// type), render pass descriptors from the declared attachments, and transients from a TransientPool.
/// Created per frame; it owns nothing across frames.
///
/// The graph's barriers need no commands: every Phox resource uses Metal's automatic hazard tracking,
/// which orders encoders that touch the same resource, and Metal has no image layouts. They would become
/// MTLFence updates and waits if resources moved to untracked memory (e.g. for aliasing).
class GraphExecutor final : public phox::GraphBackend {
public:
    GraphExecutor(const RenderGraph &graph, Device &device, TransientPool &pool, std::uint64_t frameNumber,
                  id<MTLCommandBuffer> commandBuffer);
    /// Returns pooled resources still held, e.g. after an exception while encoding.
    ~GraphExecutor() override;
    GraphExecutor(const GraphExecutor &) = delete;
    GraphExecutor &operator=(const GraphExecutor &) = delete;

    /// Supplies the Metal objects behind imported resources (e.g. a drawable's texture).
    void bindImage(ImageHandle handle, id<MTLTexture> texture);
    void bindBuffer(BufferHandle handle, id<MTLBuffer> buffer);

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
        id<MTLTexture> texture = nil;
        Image *pooled = nullptr;
    };
    struct BufferSlot {
        id<MTLBuffer> buffer = nil;
        Buffer *pooled = nullptr;
    };

    ImageSlot &imageSlot(std::uint32_t index);
    id<MTLTexture> boundTexture(std::uint32_t index) const;
    BufferSlot &bufferSlot(std::uint32_t index);
    id<MTLBuffer> boundBuffer(std::uint32_t index) const;

    const RenderGraph &m_graph;
    Device &m_device;
    TransientPool &m_pool;
    std::uint64_t m_frameNumber;
    id<MTLCommandBuffer> m_commandBuffer;
    id<MTLCommandEncoder> m_encoder = nil; ///< the running pass's encoder
    PassType m_passType = PassType::Graphics;
    PassContext m_context;
    std::vector<ImageSlot> m_images;
    std::vector<BufferSlot> m_buffers;
};

} // namespace phox::mtl

#endif // PHOX_METAL_GRAPH_EXECUTOR_HPP
