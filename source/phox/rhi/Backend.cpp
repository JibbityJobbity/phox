#include "phox/rhi/Backend.hpp"

#ifdef PHOX_HAS_NULL
#include "phox/null/NullDevice.hpp"
#endif
#ifdef PHOX_HAS_VULKAN
#include "phox/vk/VulkanBackend.hpp"
#endif

#include <stdexcept>

namespace phox::rhi {

std::unique_ptr<Device> createDevice(Backend backend, const DeviceOptions &options) {
#if !defined(PHOX_HAS_VULKAN)
    (void)options;
#endif
    switch (backend) {
    case Backend::Vulkan:
#ifdef PHOX_HAS_VULKAN
        return vulkan::createDevice(options);
#else
        throw std::runtime_error("Vulkan backend is not available: built with PHOX_BACKEND_VULKAN=OFF");
#endif
    case Backend::Null:
#ifdef PHOX_HAS_NULL
        return std::make_unique<null::NullDevice>();
#else
        throw std::runtime_error("Null backend is not available: built with PHOX_BACKEND_NULL=OFF");
#endif
    }
    throw std::runtime_error("Unknown backend");
}

} // namespace phox::rhi
