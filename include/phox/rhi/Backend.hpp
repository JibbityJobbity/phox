#ifndef PHOX_RHI_BACKEND_HPP
#define PHOX_RHI_BACKEND_HPP

#include "phox/rhi/Device.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace phox::rhi {

enum class Backend { Vulkan, Null };
struct DeviceOptions {
    bool validation = false;
    std::optional<uint32_t> adapterIndex;
};
// Throws std::runtime_error naming the backend and reason. Never falls back to another backend.
std::unique_ptr<Device> createDevice(Backend, const DeviceOptions & = {});

} // namespace phox::rhi

#endif // PHOX_RHI_BACKEND_HPP
