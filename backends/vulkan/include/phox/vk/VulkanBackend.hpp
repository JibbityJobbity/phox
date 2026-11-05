#ifndef PHOX_VK_VULKAN_BACKEND_HPP
#define PHOX_VK_VULKAN_BACKEND_HPP

#include "phox/rhi/Backend.hpp"

#include <memory>

// The Vulkan backend lives in its own namespace so it can coexist with the legacy `phox::vk`
// renderer until that is removed.
namespace phox::vulkan {

/// Creates a headless Vulkan 1.3 device. Throws std::runtime_error naming the reason when the loader,
/// a suitable GPU, or a required feature is missing.
std::unique_ptr<rhi::Device> createDevice(const rhi::DeviceOptions &options);

} // namespace phox::vulkan

#endif // PHOX_VK_VULKAN_BACKEND_HPP
