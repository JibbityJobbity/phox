#include "phox/vulkan/GraphExecutor.hpp"

#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/Image.hpp"
#include "phox/vulkan/TransientPool.hpp"

#include <stdexcept>

namespace phox::vk {

namespace {

struct UsageState {
    VkImageLayout layout;
    VkAccessFlags access;
    VkPipelineStageFlags stage;
};

UsageState stateFor(ImageUsage usage) {
    switch (usage) {
    case ImageUsage::Undefined:
        // Only ever a source. ALL_COMMANDS + MEMORY_WRITE orders the first use after everything earlier on
        // the queue: a semaphore wait for an acquired swapchain image, or a previous user of a pooled image.
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    case ImageUsage::ColorAttachment:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    case ImageUsage::TransferSrc:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    case ImageUsage::TransferDst:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    case ImageUsage::Present:
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
    default:
        break;
    }
    throw std::logic_error("image usage not supported by this executor yet");
}

} // namespace

GraphExecutor::GraphExecutor(Device &device, TransientPool &pool, std::uint64_t frame, VkCommandBuffer commandBuffer)
    : m_device(device), m_pool(pool), m_frame(frame), m_commandBuffer(commandBuffer) {}

GraphExecutor::~GraphExecutor() {
    for (Slot &s : m_slots) {
        if (s.pooled != nullptr) {
            m_pool.release(*s.pooled, m_frame);
        }
    }
}

GraphExecutor::Slot &GraphExecutor::slot(ResourceHandle handle) {
    if (handle.id >= m_slots.size()) {
        m_slots.resize(handle.id + 1);
    }
    return m_slots[handle.id];
}

const GraphExecutor::Slot &GraphExecutor::slot(ResourceHandle handle) const {
    if (handle.id >= m_slots.size() || m_slots[handle.id].image == VK_NULL_HANDLE) {
        throw std::logic_error("graph image has no backing VkImage");
    }
    return m_slots[handle.id];
}

void GraphExecutor::bindImported(ResourceHandle handle, VkImage image, VkImageView view) {
    Slot &s = slot(handle);
    s.image = image;
    s.view = view;
}

VkImage GraphExecutor::image(ResourceHandle handle) const { return slot(handle).image; }

VkImageView GraphExecutor::view(ResourceHandle handle) const {
    const Slot &s = slot(handle);
    if (s.view == VK_NULL_HANDLE) {
        throw std::logic_error("graph image has no view");
    }
    return s.view;
}

void GraphExecutor::allocate(const ImageInfo &info) {
    ImageCreateInfo createInfo;
    createInfo.extent = {info.desc.width, info.desc.height};
    createInfo.format = info.desc.format;
    createInfo.usage = info.usageMask; // exactly the usages the compiled graph needs
    createInfo.name = info.name;
    Image &image = m_pool.acquire(createInfo, m_frame);
    Slot &s = slot(info.handle);
    s.image = image.handle();
    s.view = image.view();
    s.pooled = &image;
}

void GraphExecutor::release(const ImageInfo &info) {
    // The image may be handed out again immediately; see TransientPool for why that is safe. The handles
    // stay in the slot so passes that ran earlier in this recording are unaffected.
    Slot &s = slot(info.handle);
    if (s.pooled != nullptr) {
        m_pool.release(*s.pooled, m_frame);
        s.pooled = nullptr;
    }
}

void GraphExecutor::transition(const ImageInfo &info, ImageUsage from, ImageUsage to) {
    const UsageState src = stateFor(from);
    const UsageState dst = stateFor(to);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = src.layout;
    barrier.newLayout = dst.layout;
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image(info.handle);
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    m_device.table().vkCmdPipelineBarrier(m_commandBuffer, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1,
                                          &barrier);
}

} // namespace phox::vk
