#include "phox/vulkan/GraphExecutor.hpp"

#include "phox/vulkan/Device.hpp"

#include <stdexcept>

namespace phox {
namespace vk {

namespace {

struct UsageState {
    VkImageLayout layout;
    VkAccessFlags access;
    VkPipelineStageFlags stage;
};

UsageState stateFor(ImageUsage usage) {
    switch (usage) {
    case ImageUsage::Undefined:
        // Used only as a source scope. ALL_COMMANDS (not TOP_OF_PIPE) so that a transition out of
        // Undefined is ordered after a semaphore wait on the first use (e.g. an acquired swapchain image).
        return {VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
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
    }
    throw std::logic_error("Unknown ImageUsage");
}

VkFormat toVkFormat(ImageFormat format) {
    switch (format) {
    case ImageFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    throw std::logic_error("Unknown ImageFormat");
}

VkImageUsageFlags toVkUsage(std::uint32_t usageMask) {
    VkImageUsageFlags flags = 0;
    if (usageMask & usageBit(ImageUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usageMask & usageBit(ImageUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usageMask & usageBit(ImageUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

} // namespace

GraphExecutor::GraphExecutor(Device &device) : m_device(device) {}

GraphExecutor::~GraphExecutor() {
    if (m_device.handle() == VK_NULL_HANDLE) {
        return;
    }
    const auto &table = m_device.table();
    table.vkDeviceWaitIdle(m_device.handle());
    for (auto &s : m_slots) {
        if (!s.owned) {
            continue;
        }
        table.vkDestroyImageView(m_device.handle(), s.view, nullptr);
        if (s.image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_device.allocator(), s.image, s.allocation);
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

void GraphExecutor::bindImported(ResourceHandle handle, VkImage image) {
    Slot &s = slot(handle);
    s.image = image;
    s.owned = false;
}

VkImage GraphExecutor::image(ResourceHandle handle) const { return slot(handle).image; }

VkImageView GraphExecutor::view(ResourceHandle handle) const {
    const Slot &s = slot(handle);
    if (s.view == VK_NULL_HANDLE) {
        throw std::logic_error("graph image has no view (imported images are not given one)");
    }
    return s.view;
}

void GraphExecutor::allocate(const ImageInfo &info) {
    Slot &s = slot(info.handle);
    const VkFormat format = toVkFormat(info.desc.format);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {info.desc.width, info.desc.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkUsage(info.usageMask); // exactly the usages the compiled graph needs
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_device.allocator(), &imageInfo, &allocationInfo, &s.image, &s.allocation, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create graph image '" + info.name + "'");
    }
    s.owned = true;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = s.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (m_device.table().vkCreateImageView(m_device.handle(), &viewInfo, nullptr, &s.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create view for graph image '" + info.name + "'");
    }
}

void GraphExecutor::release(const ImageInfo &) {
    // Deliberately empty: the recorded commands may not have executed yet. The destructor frees
    // transient images once the device is idle.
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

} // namespace vk
} // namespace phox
