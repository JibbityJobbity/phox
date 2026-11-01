#ifndef PHOX_VULKAN_PASS_TIMER_HPP
#define PHOX_VULKAN_PASS_TIMER_HPP

#include "phox/Renderer.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phox::vk {

class Device;

/// GPU timestamps around the passes of one frame. Each FrameContext has one: reset() starts a frame's
/// recording, begin() and end() bracket each pass, and collect() reads the times once the frame's fence
/// has signalled. Does nothing on queues without timestamps.
class PassTimer {
public:
    static constexpr std::uint32_t kMaxPasses = 32;

    explicit PassTimer(Device &device);
    ~PassTimer();
    PassTimer(const PassTimer &) = delete;
    PassTimer &operator=(const PassTimer &) = delete;

    /// Records the reset of the queries into @p cmd (outside any rendering) and forgets the passes of the
    /// previous frame, which must have been collected.
    void reset(VkCommandBuffer cmd);
    void begin(VkCommandBuffer cmd, const std::string &name);
    void end(VkCommandBuffer cmd);
    /// Reads the times of the frame recorded since reset(). Call once its fence has signalled; calling it
    /// again before the next reset() changes nothing.
    void collect();
    /// The passes of the last collected frame, in execution order.
    const std::vector<PassTiming> &timings() const { return m_timings; }

private:
    Device &m_device;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    double m_nanosecondsPerTick = 0.0;
    std::uint64_t m_validMask = 0;
    std::vector<std::string> m_recorded; ///< passes begun since reset(), in query order
    bool m_open = false;                 ///< a pass is begun but not ended
    bool m_pending = false;              ///< recorded queries not collected yet
    std::vector<PassTiming> m_timings;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_PASS_TIMER_HPP
