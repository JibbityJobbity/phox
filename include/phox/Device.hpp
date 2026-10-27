#ifndef PHOX_DEVICE_HPP
#define PHOX_DEVICE_HPP

#include "phox/Buffer.hpp"
#include "phox/Image.hpp"
#include "phox/Resources.hpp"

#include <memory>

namespace phox {

/// Creates GPU resources. Backends add their own entry points; this is the part that needs no
/// backend types.
class Device {
public:
    virtual ~Device() = default;
    virtual std::unique_ptr<Buffer> createBuffer(const BufferCreateInfo &info) = 0;
    virtual std::unique_ptr<Image> createImage(const ImageCreateInfo &info) = 0;
    /// Blocks until the GPU is idle. For teardown and swapchain recreation, not for every frame.
    virtual void waitIdle() = 0;
};

}

#endif // PHOX_DEVICE_HPP
