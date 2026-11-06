#include "phox/vulkan/FrameContext.hpp"

#include "phox/vulkan/Device.hpp"

#include <stdexcept>
#include <string>

namespace phox::vk {

FrameContext::FrameContext(Device &device, std::uint32_t index)
    : m_device(device), m_index(index), m_arena(device), m_descriptors(device), m_timer(device) {
    const VkDevice dev = device.handle();
    const auto &table = device.table();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamilyIndex();
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the first begin() must not wait
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (table.vkCreateCommandPool(dev, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS ||
        table.vkCreateFence(dev, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS ||
        table.vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &m_imageAvailable) != VK_SUCCESS) {
        table.vkDestroySemaphore(dev, m_imageAvailable, nullptr);
        table.vkDestroyFence(dev, m_fence, nullptr);
        table.vkDestroyCommandPool(dev, m_commandPool, nullptr);
        throw std::runtime_error("failed to create frame " + std::to_string(index) + " sync objects");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (table.vkAllocateCommandBuffers(dev, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        table.vkDestroySemaphore(dev, m_imageAvailable, nullptr);
        table.vkDestroyFence(dev, m_fence, nullptr);
        table.vkDestroyCommandPool(dev, m_commandPool, nullptr);
        throw std::runtime_error("failed to allocate frame " + std::to_string(index) + " command buffer");
    }

    const std::string name = "frame " + std::to_string(index);
    device.setObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(m_commandBuffer), name);
    device.setObjectName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<std::uint64_t>(m_fence), name);
    device.setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(m_imageAvailable),
                         name + " image available");

    if (device.asyncCompute()) {
        poolInfo.queueFamilyIndex = device.queueFamilyIndex(Queue::AsyncCompute);
        if (table.vkCreateCommandPool(dev, &poolInfo, nullptr, &m_computePool) != VK_SUCCESS) {
            table.vkDestroySemaphore(dev, m_imageAvailable, nullptr);
            table.vkDestroyFence(dev, m_fence, nullptr);
            table.vkDestroyCommandPool(dev, m_commandPool, nullptr);
            throw std::runtime_error("failed to create frame " + std::to_string(index) + " compute command pool");
        }
    }
}

VkCommandBuffer FrameContext::extraCommandBuffer(Queue queue) {
    Extra &extra = m_extra[static_cast<std::size_t>(queue)];
    if (extra.used == extra.buffers.size()) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = queue == Queue::AsyncCompute ? m_computePool : m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if (allocInfo.commandPool == VK_NULL_HANDLE) {
            throw std::logic_error("the device has no async compute queue");
        }
        VkCommandBuffer buffer = VK_NULL_HANDLE;
        if (m_device.table().vkAllocateCommandBuffers(m_device.handle(), &allocInfo, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate a command buffer");
        }
        m_device.setObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(buffer),
                               "frame " + std::to_string(m_index) + (queue == Queue::AsyncCompute ? " compute " : " graphics ") +
                                   std::to_string(extra.buffers.size() + 1));
        extra.buffers.push_back(buffer);
    }
    return extra.buffers[extra.used++];
}

FrameContext::~FrameContext() {
    // The renderer waits for the device before destroying frames, so nothing here is still in use.
    m_deferred.flush();
    const auto &table = m_device.table();
    table.vkDestroySemaphore(m_device.handle(), m_imageAvailable, nullptr);
    table.vkDestroyFence(m_device.handle(), m_fence, nullptr);
    table.vkDestroyCommandPool(m_device.handle(), m_commandPool, nullptr);
    table.vkDestroyCommandPool(m_device.handle(), m_computePool, nullptr);
}

void FrameContext::begin() {
    const auto &table = m_device.table();
    if (table.vkWaitForFences(m_device.handle(), 1, &m_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("waiting for a frame fence failed");
    }
    m_device.waitTimeline(Queue::AsyncCompute, m_computeWait);
    m_computeWait = 0;
    m_timer.collect();
    m_deferred.flush();
    table.vkResetCommandPool(m_device.handle(), m_commandPool, 0);
    if (m_computePool != VK_NULL_HANDLE) {
        table.vkResetCommandPool(m_device.handle(), m_computePool, 0);
    }
    for (Extra &extra : m_extra) extra.used = 0;
    m_arena.reset();
    m_descriptors.reset();
}

} // namespace phox::vk
