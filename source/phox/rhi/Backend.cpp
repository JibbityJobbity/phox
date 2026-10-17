#include "phox/rhi/Backend.hpp"

#ifdef PHOX_HAS_NULL
#include "phox/null/NullDevice.hpp"
#endif

#include <stdexcept>

namespace phox::rhi {

std::unique_ptr<Device> createDevice(Backend backend, const DeviceOptions &options) {
    (void)options;
    switch (backend) {
    case Backend::Vulkan:
#ifdef PHOX_HAS_VULKAN
        throw std::logic_error("PHOX_HAS_VULKAN is defined but no factory is wired up yet");
#else
        throw std::runtime_error("Vulkan backend is not available: this build has no RHI Vulkan backend");
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
