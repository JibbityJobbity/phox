#include "phox/vulkan/GraphExecutor.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/Image.hpp"
#include "phox/vulkan/TransientPool.hpp"

#include <stdexcept>

namespace phox::vk {

namespace {

// Where and how a usage touches memory: the synchronization2 scope on one side of a barrier.
struct Scope {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr VkAccessFlags2 kWriteAccess =
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
    VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

// Shader stages are not part of the usage, so they come from the kind of pass using the resource.
// Graphics passes include the task and mesh stages where the device has them.
VkPipelineStageFlags2 shaderStages(std::optional<PassType> pass, bool meshShaders) {
    if (pass == PassType::Graphics) {
        return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
               (meshShaders ? VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT : 0);
    }
    if (pass == PassType::Compute) return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

Scope imageScope(ImageUsage usage, std::optional<PassType> pass, bool meshShaders) {
    switch (usage) {
    case ImageUsage::Undefined:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case ImageUsage::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ImageUsage::DepthAttachment:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case ImageUsage::DepthReadOnly:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                    shaderStages(pass, meshShaders),
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case ImageUsage::Sampled:
        return {shaderStages(pass, meshShaders), VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ImageUsage::StorageRead:
        return {shaderStages(pass, meshShaders), VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ImageUsage::StorageWrite:
        return {shaderStages(pass, meshShaders),
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ImageUsage::TransferSrc:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ImageUsage::TransferDst:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ImageUsage::Present:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    throw std::logic_error("unknown ImageUsage");
}

Scope bufferScope(BufferUsage usage, std::optional<PassType> pass, bool meshShaders) {
    switch (usage) {
    case BufferUsage::Undefined: return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT};
    case BufferUsage::Vertex: return {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    case BufferUsage::Index: return {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT};
    case BufferUsage::Uniform: return {shaderStages(pass, meshShaders), VK_ACCESS_2_UNIFORM_READ_BIT};
    case BufferUsage::StorageRead: return {shaderStages(pass, meshShaders), VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    case BufferUsage::StorageWrite:
        return {shaderStages(pass, meshShaders),
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    case BufferUsage::Indirect: return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT};
    case BufferUsage::TransferSrc: return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
    case BufferUsage::TransferDst: return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case BufferUsage::HostRead: return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT};
    case BufferUsage::HostWrite: return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT};
    }
    throw std::logic_error("unknown BufferUsage");
}

// The first half of a barrier: what must finish, and which writes must be made available. A state the
// resource entered the graph in (no pass) may have been produced by anything earlier on the queue.
Scope sourceScope(Scope scope, bool external) {
    if (external) {
        scope.stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        scope.access = VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    scope.access &= kWriteAccess; // reads need no availability operation
    return scope;
}

// The second half: what waits. A state the resource leaves the graph in is consumed by anything later,
// except presentation (ordered by the submission's semaphore) and host reads (ordered by the fence).
Scope destinationScope(Scope scope, bool external, bool present) {
    if (external && !present && scope.stages != VK_PIPELINE_STAGE_2_HOST_BIT) {
        scope.stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    return scope;
}

VkAttachmentLoadOp toVk(LoadOp op) {
    switch (op) {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp toVk(StoreOp op) {
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

} // namespace

// ---- PassContext ------------------------------------------------------------------------------------

VkCommandBuffer PassContext::cmd() const { return m_executor.m_commandBuffer; }
VkImage PassContext::image(ImageHandle handle) const { return m_executor.boundImage(handle.index).image; }
VkImageView PassContext::view(ImageHandle handle) const {
    const VkImageView view = m_executor.boundImage(handle.index).view;
    if (view == VK_NULL_HANDLE) {
        throw std::logic_error("image '" + m_executor.m_graph.imageInfo(handle).name + "' was bound without a view");
    }
    return view;
}
VkImageView PassContext::mipView(ImageHandle handle, std::uint32_t level) const {
    Image *owner = m_executor.boundImage(handle.index).owner;
    if (owner == nullptr) {
        throw std::logic_error("image '" + m_executor.m_graph.imageInfo(handle).name +
                               "' is imported; only transient images have mip views");
    }
    return owner->mipView(level);
}
Extent2D PassContext::extent(ImageHandle handle) const { return m_executor.m_graph.imageInfo(handle).extent; }
VkBuffer PassContext::buffer(BufferHandle handle) const { return m_executor.boundBuffer(handle.index).buffer; }
Device &PassContext::device() const { return m_executor.m_device; }
FrameContext &PassContext::frame() const { return m_executor.m_frame; }

// ---- GraphExecutor ----------------------------------------------------------------------------------

GraphExecutor::GraphExecutor(const RenderGraph &graph, Device &device, TransientPool &pool, FrameContext &frame,
                             std::uint64_t frameNumber, VkCommandBuffer commandBuffer)
    : m_graph(graph), m_device(device), m_pool(pool), m_frame(frame), m_frameNumber(frameNumber),
      m_commandBuffer(commandBuffer), m_context(*this) {
    m_frame.timer().reset(m_commandBuffer);
}

GraphExecutor::~GraphExecutor() {
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

const GraphExecutor::ImageSlot &GraphExecutor::boundImage(std::uint32_t index) const {
    if (index >= m_images.size() || m_images[index].image == VK_NULL_HANDLE) {
        throw std::logic_error("image '" + m_graph.imageInfo(index).name + "' has no VkImage bound");
    }
    return m_images[index];
}

GraphExecutor::BufferSlot &GraphExecutor::bufferSlot(std::uint32_t index) {
    if (index >= m_buffers.size()) m_buffers.resize(index + 1);
    return m_buffers[index];
}

const GraphExecutor::BufferSlot &GraphExecutor::boundBuffer(std::uint32_t index) const {
    if (index >= m_buffers.size() || m_buffers[index].buffer == VK_NULL_HANDLE) {
        throw std::logic_error("buffer '" + m_graph.bufferInfo(index).name + "' has no VkBuffer bound");
    }
    return m_buffers[index];
}

void GraphExecutor::bindImage(ImageHandle handle, VkImage image, VkImageView view) {
    ImageSlot &slot = imageSlot(handle.index);
    slot.image = image;
    slot.view = view;
}

void GraphExecutor::bindBuffer(BufferHandle handle, VkBuffer buffer) { bufferSlot(handle.index).buffer = buffer; }

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
    slot.image = image.handle();
    slot.view = image.view();
    slot.pooled = &image;
    slot.owner = &image;
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

// Released resources may be handed out again straight away (see TransientPool). The handles stay in the
// slot: passes recorded earlier keep referring to them, which is fine.
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
    const bool meshShaders = m_device.meshShaders();
    std::vector<VkImageMemoryBarrier2> images;
    images.reserve(batch.images.size());
    for (const ImageBarrier &b : batch.images) {
        const ImageInfo &info = m_graph.imageInfo(b.image);
        const Scope src = sourceScope(imageScope(b.from, b.fromPass, meshShaders), !b.fromPass);
        const Scope dst =
            destinationScope(imageScope(b.to, b.toPass, meshShaders), !b.toPass, b.to == ImageUsage::Present);
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = src.stages;
        barrier.srcAccessMask = src.access;
        barrier.dstStageMask = dst.stages;
        barrier.dstAccessMask = dst.access;
        barrier.oldLayout = src.layout;
        barrier.newLayout = dst.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = boundImage(b.image).image;
        barrier.subresourceRange = {aspectMask(info.desc.format), 0, VK_REMAINING_MIP_LEVELS, 0,
                                    VK_REMAINING_ARRAY_LAYERS};
        images.push_back(barrier);
    }
    std::vector<VkBufferMemoryBarrier2> buffers;
    buffers.reserve(batch.buffers.size());
    for (const BufferBarrier &b : batch.buffers) {
        const Scope src = sourceScope(bufferScope(b.from, b.fromPass, meshShaders), !b.fromPass);
        const Scope dst = destinationScope(bufferScope(b.to, b.toPass, meshShaders), !b.toPass, false);
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = src.stages;
        barrier.srcAccessMask = src.access;
        barrier.dstStageMask = dst.stages;
        barrier.dstAccessMask = dst.access;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = boundBuffer(b.buffer).buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        buffers.push_back(barrier);
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(images.size());
    dependency.pImageMemoryBarriers = images.data();
    dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(buffers.size());
    dependency.pBufferMemoryBarriers = buffers.data();
    m_device.table().vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void GraphExecutor::beginPass(const CompiledPass &pass) {
    if (m_device.debugUtils()) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = pass.name.c_str();
        vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
    }
    m_frame.timer().begin(m_commandBuffer, pass.name);
    if (pass.type != PassType::Graphics) {
        return;
    }

    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(pass.colorAttachments.size());
    for (const Attachment &attachment : pass.colorAttachments) {
        VkRenderingAttachmentInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView = m_context.view({attachment.image, 0});
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp = toVk(attachment.load);
        info.storeOp = toVk(attachment.store);
        info.clearValue.color = {{attachment.clearColor[0], attachment.clearColor[1], attachment.clearColor[2],
                                  attachment.clearColor[3]}};
        colors.push_back(info);
    }
    VkRenderingAttachmentInfo depth{};
    bool stencil = false;
    if (pass.depthAttachment) {
        const Attachment &attachment = *pass.depthAttachment;
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = m_context.view({attachment.image, 0});
        depth.imageLayout = attachment.readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = toVk(attachment.load);
        // A read-only attachment must not store: STORE counts as a write for synchronization.
        depth.storeOp = attachment.readOnly ? VK_ATTACHMENT_STORE_OP_NONE : toVk(attachment.store);
        depth.clearValue.depthStencil = {attachment.clearDepth, 0};
        stencil = hasStencil(m_graph.imageInfo(attachment.image).desc.format);
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {pass.renderArea.width, pass.renderArea.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    rendering.pColorAttachments = colors.data();
    rendering.pDepthAttachment = pass.depthAttachment ? &depth : nullptr;
    rendering.pStencilAttachment = stencil ? &depth : nullptr;
    const auto &table = m_device.table();
    table.vkCmdBeginRendering(m_commandBuffer, &rendering);

    // Every pipeline uses dynamic viewport and scissor; default both to the whole render area.
    const VkViewport viewport{0.0f, 0.0f, float(pass.renderArea.width), float(pass.renderArea.height), 0.0f, 1.0f};
    table.vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    table.vkCmdSetScissor(m_commandBuffer, 0, 1, &rendering.renderArea);
}

void GraphExecutor::endPass(const CompiledPass &pass) {
    if (pass.type == PassType::Graphics) {
        m_device.table().vkCmdEndRendering(m_commandBuffer);
    }
    m_frame.timer().end(m_commandBuffer);
    if (m_device.debugUtils()) {
        vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
    }
}

VkPipelineStageFlags2 firstUseStage(const RenderGraph &graph, ImageHandle image, bool meshShaders) {
    const auto first = graph.firstUse(image);
    if (!first) {
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    return imageScope(first->usage, first->pass, meshShaders).stages;
}

} // namespace phox::vk
