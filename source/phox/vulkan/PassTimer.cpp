#include "phox/vulkan/PassTimer.hpp"

#include "phox/vulkan/Device.hpp"

#include <stdexcept>

namespace phox::vk {

PassTimer::PassTimer(Device &device) : m_device(device) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device.physicalDevice(), &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device.physicalDevice(), &count, families.data());
    const std::uint32_t validBits = families.at(device.graphicsQueueFamilyIndex()).timestampValidBits;
    const float period = device.properties().limits.timestampPeriod;
    if (validBits == 0 || period <= 0.0f) {
        return; // no timestamps on this queue: timings stay empty
    }
    m_validMask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1;
    m_nanosecondsPerTick = period;

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2 * kMaxPasses;
    if (device.table().vkCreateQueryPool(device.handle(), &info, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create a timestamp query pool");
    }
}

PassTimer::~PassTimer() { m_device.table().vkDestroyQueryPool(m_device.handle(), m_pool, nullptr); }

void PassTimer::reset(VkCommandBuffer cmd) {
    m_recorded.clear();
    m_open = false;
    m_pending = false;
    if (m_pool != VK_NULL_HANDLE) {
        m_device.table().vkCmdResetQueryPool(cmd, m_pool, 0, 2 * kMaxPasses);
    }
}

void PassTimer::begin(VkCommandBuffer cmd, const std::string &name) {
    if (m_pool == VK_NULL_HANDLE || m_recorded.size() >= kMaxPasses) {
        return;
    }
    const auto query = static_cast<std::uint32_t>(2 * m_recorded.size());
    m_device.table().vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_pool, query);
    m_recorded.push_back(name);
    m_open = true;
    m_pending = true;
}

void PassTimer::end(VkCommandBuffer cmd) {
    if (!m_open) {
        return;
    }
    const auto query = static_cast<std::uint32_t>(2 * m_recorded.size() - 1);
    m_device.table().vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_pool, query);
    m_open = false;
}

void PassTimer::collect() {
    if (!m_pending || m_open) {
        return;
    }
    m_pending = false;
    std::vector<std::uint64_t> ticks(2 * m_recorded.size());
    const VkResult result = m_device.table().vkGetQueryPoolResults(
        m_device.handle(), m_pool, 0, static_cast<std::uint32_t>(ticks.size()), ticks.size() * sizeof(std::uint64_t),
        ticks.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS) {
        return; // keep the previous frame's timings
    }
    m_timings.clear();
    for (std::size_t i = 0; i < m_recorded.size(); ++i) {
        const std::uint64_t elapsed = ((ticks[2 * i + 1] - ticks[2 * i]) & m_validMask);
        m_timings.push_back({m_recorded[i], static_cast<double>(elapsed) * m_nanosecondsPerTick * 1e-6});
    }
}

} // namespace phox::vk
