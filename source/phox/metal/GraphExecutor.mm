#include "phox/metal/GraphExecutor.hpp"

#include "phox/metal/Buffer.hpp"
#include "phox/metal/Conversions.hpp"
#include "phox/metal/Device.hpp"
#include "phox/metal/Image.hpp"
#include "phox/metal/TransientPool.hpp"

#include <stdexcept>
#include <string>

namespace phox::mtl {

namespace {

const char *passTypeName(PassType type) {
    switch (type) {
    case PassType::Graphics: return "graphics";
    case PassType::Compute: return "compute";
    case PassType::Transfer: return "transfer";
    }
    return "unknown";
}

} // namespace

// ---- PassContext ------------------------------------------------------------------------------------

id<MTLCommandBuffer> PassContext::commandBuffer() const { return m_executor.m_commandBuffer; }

id<MTLRenderCommandEncoder> PassContext::renderEncoder() const {
    if (m_executor.m_encoder == nil || m_executor.m_passType != PassType::Graphics) {
        throw std::logic_error(std::string("renderEncoder() in a ") + passTypeName(m_executor.m_passType) + " pass");
    }
    return (id<MTLRenderCommandEncoder>)m_executor.m_encoder;
}

id<MTLComputeCommandEncoder> PassContext::computeEncoder() const {
    if (m_executor.m_encoder == nil || m_executor.m_passType != PassType::Compute) {
        throw std::logic_error(std::string("computeEncoder() in a ") + passTypeName(m_executor.m_passType) + " pass");
    }
    return (id<MTLComputeCommandEncoder>)m_executor.m_encoder;
}

id<MTLBlitCommandEncoder> PassContext::blitEncoder() const {
    if (m_executor.m_encoder == nil || m_executor.m_passType != PassType::Transfer) {
        throw std::logic_error(std::string("blitEncoder() in a ") + passTypeName(m_executor.m_passType) + " pass");
    }
    return (id<MTLBlitCommandEncoder>)m_executor.m_encoder;
}

id<MTLTexture> PassContext::texture(ImageHandle handle) const { return m_executor.boundTexture(handle.index); }
Extent2D PassContext::extent(ImageHandle handle) const { return m_executor.m_graph.imageInfo(handle).extent; }
id<MTLBuffer> PassContext::buffer(BufferHandle handle) const { return m_executor.boundBuffer(handle.index); }
Device &PassContext::device() const { return m_executor.m_device; }

// ---- GraphExecutor ----------------------------------------------------------------------------------

GraphExecutor::GraphExecutor(const RenderGraph &graph, Device &device, TransientPool &pool, std::uint64_t frameNumber,
                             id<MTLCommandBuffer> commandBuffer)
    : m_graph(graph), m_device(device), m_pool(pool), m_frameNumber(frameNumber), m_commandBuffer(commandBuffer),
      m_context(*this) {}

GraphExecutor::~GraphExecutor() {
    // An encoder left open (a pass threw) must be ended before the command buffer can be released.
    if (m_encoder != nil) {
        [m_encoder endEncoding];
    }
    for (ImageSlot &slot : m_images) {
        if (slot.pooled != nullptr) m_pool.release(*slot.pooled, m_frameNumber);
    }
    for (BufferSlot &slot : m_buffers) {
        if (slot.pooled != nullptr) m_pool.release(*slot.pooled, m_frameNumber);
    }
}

GraphExecutor::ImageSlot &GraphExecutor::imageSlot(std::uint32_t index) {
    if (index >= m_images.size()) m_images.resize(index + 1);
    return m_images[index];
}

id<MTLTexture> GraphExecutor::boundTexture(std::uint32_t index) const {
    if (index >= m_images.size() || m_images[index].texture == nil) {
        throw std::logic_error("image '" + m_graph.imageInfo(index).name + "' has no MTLTexture bound");
    }
    return m_images[index].texture;
}

GraphExecutor::BufferSlot &GraphExecutor::bufferSlot(std::uint32_t index) {
    if (index >= m_buffers.size()) m_buffers.resize(index + 1);
    return m_buffers[index];
}

id<MTLBuffer> GraphExecutor::boundBuffer(std::uint32_t index) const {
    if (index >= m_buffers.size() || m_buffers[index].buffer == nil) {
        throw std::logic_error("buffer '" + m_graph.bufferInfo(index).name + "' has no MTLBuffer bound");
    }
    return m_buffers[index].buffer;
}

void GraphExecutor::bindImage(ImageHandle handle, id<MTLTexture> texture) { imageSlot(handle.index).texture = texture; }

void GraphExecutor::bindBuffer(BufferHandle handle, id<MTLBuffer> buffer) { bufferSlot(handle.index).buffer = buffer; }

void GraphExecutor::acquire(const ImageInfo &info) {
    ImageCreateInfo createInfo;
    createInfo.extent = info.extent;
    createInfo.format = info.desc.format;
    createInfo.mipLevels = info.desc.mipLevels;
    createInfo.arrayLayers = info.desc.arrayLayers;
    createInfo.usage = info.usage; // exactly the usages the compiled graph needs
    createInfo.name = info.name;
    Image &image = m_pool.acquire(createInfo, m_frameNumber);
    ImageSlot &slot = imageSlot(info.index);
    slot.texture = image.handle();
    slot.pooled = &image;
}

void GraphExecutor::acquire(const BufferInfo &info) {
    BufferCreateInfo createInfo;
    createInfo.size = info.desc.size;
    createInfo.usage = info.usage;
    createInfo.memory = MemoryLocation::DeviceLocal;
    createInfo.name = info.name;
    Buffer &buffer = m_pool.acquire(createInfo, m_frameNumber);
    BufferSlot &slot = bufferSlot(info.index);
    slot.buffer = buffer.handle();
    slot.pooled = &buffer;
}

// Released resources may be handed out again straight away (see TransientPool). The objects stay in the
// slot: encoders recorded earlier keep referring to them, which is fine.
void GraphExecutor::release(const ImageInfo &info) {
    ImageSlot &slot = imageSlot(info.index);
    if (slot.pooled != nullptr) {
        m_pool.release(*slot.pooled, m_frameNumber);
        slot.pooled = nullptr;
    }
}

void GraphExecutor::release(const BufferInfo &info) {
    BufferSlot &slot = bufferSlot(info.index);
    if (slot.pooled != nullptr) {
        m_pool.release(*slot.pooled, m_frameNumber);
        slot.pooled = nullptr;
    }
}

void GraphExecutor::barriers(const BarrierBatch &batch) {
    // Hazard tracking orders the passes (see the class comment). Still check that every resource a
    // barrier names is bound, as the Vulkan executor does, so a missing bindImage fails the same way.
    for (const ImageBarrier &b : batch.images) boundTexture(b.image);
    for (const BufferBarrier &b : batch.buffers) boundBuffer(b.buffer);
}

void GraphExecutor::beginPass(const CompiledPass &pass) {
    m_passType = pass.type;
    NSString *label = [NSString stringWithUTF8String:pass.name.c_str()];

    if (pass.type == PassType::Transfer) {
        m_encoder = [m_commandBuffer blitCommandEncoder];
    } else if (pass.type == PassType::Compute) {
        m_encoder = [m_commandBuffer computeCommandEncoder];
    } else {
        MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        for (std::size_t i = 0; i < pass.colorAttachments.size(); ++i) {
            const Attachment &attachment = pass.colorAttachments[i];
            MTLRenderPassColorAttachmentDescriptor *color = descriptor.colorAttachments[i];
            color.texture = boundTexture(attachment.image);
            color.loadAction = toMtl(attachment.load);
            color.storeAction = toMtl(attachment.store);
            color.clearColor = MTLClearColorMake(attachment.clearColor[0], attachment.clearColor[1],
                                                 attachment.clearColor[2], attachment.clearColor[3]);
        }
        if (pass.depthAttachment) {
            const Attachment &attachment = *pass.depthAttachment;
            const id<MTLTexture> texture = boundTexture(attachment.image);
            // A read-only attachment keeps its contents; DontCare could discard them.
            const MTLStoreAction store = attachment.readOnly ? MTLStoreActionStore : toMtl(attachment.store);
            descriptor.depthAttachment.texture = texture;
            descriptor.depthAttachment.loadAction = toMtl(attachment.load);
            descriptor.depthAttachment.storeAction = store;
            descriptor.depthAttachment.clearDepth = attachment.clearDepth;
            if (hasStencil(m_graph.imageInfo(attachment.image).desc.format)) {
                descriptor.stencilAttachment.texture = texture;
                descriptor.stencilAttachment.loadAction = toMtl(attachment.load);
                descriptor.stencilAttachment.storeAction = store;
                descriptor.stencilAttachment.clearStencil = 0;
            }
        }
        descriptor.renderTargetWidth = pass.renderArea.width;
        descriptor.renderTargetHeight = pass.renderArea.height;
        // The viewport and scissor default to the whole render target, as the Vulkan executor sets them.
        m_encoder = [m_commandBuffer renderCommandEncoderWithDescriptor:descriptor];
    }
    if (m_encoder == nil) {
        throw std::runtime_error("failed to begin pass '" + pass.name + "'");
    }
    m_encoder.label = label;
}

void GraphExecutor::endPass(const CompiledPass &) {
    [m_encoder endEncoding];
    m_encoder = nil;
}

} // namespace phox::mtl
