#include "phox/vk/VulkanBackend.hpp"

#include "VulkanDevice.hpp"

namespace phox::vulkan {

std::unique_ptr<rhi::Device> createDevice(const rhi::DeviceOptions &options) {
    return std::make_unique<VulkanDevice>(options);
}

} // namespace phox::vulkan
