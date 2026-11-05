#ifndef PHOX_METAL_DEVICE_HPP
#define PHOX_METAL_DEVICE_HPP

#include "phox/Device.hpp"
#include "phox/metal/MetalCommon.hpp"

#include <memory>
#include <string>

namespace phox::mtl {

class Buffer;
class Image;

/// The Metal device and the one command queue Phox submits to. Every Metal buffer and texture in Phox
/// is created here.
class Device final : public phox::Device {
public:
    explicit Device(id<MTLDevice> device);
    ~Device() override;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    // phox::Device
    std::unique_ptr<phox::Buffer> createBuffer(const BufferCreateInfo &info) override;
    std::unique_ptr<phox::Image> createImage(const ImageCreateInfo &info) override;
    /// Waits for everything submitted to the queue so far (Metal has no device-wide idle).
    void waitIdle() override;

    /// Same as createBuffer/createImage, typed for code that needs the Metal objects.
    std::unique_ptr<Buffer> makeBuffer(const BufferCreateInfo &info);
    std::unique_ptr<Image> makeImage(const ImageCreateInfo &info);

    id<MTLDevice> handle() const { return m_device; }
    id<MTLCommandQueue> queue() const { return m_queue; }
    std::string name() const;

private:
    id<MTLDevice> m_device;
    id<MTLCommandQueue> m_queue;
};

} // namespace phox::mtl

#endif // PHOX_METAL_DEVICE_HPP
