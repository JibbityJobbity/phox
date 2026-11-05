#ifndef PHOX_METAL_BUFFER_HPP
#define PHOX_METAL_BUFFER_HPP

#include "phox/Buffer.hpp"
#include "phox/metal/MetalCommon.hpp"

namespace phox::mtl {

/// A Metal buffer. Upload and Readback memory is shared with the CPU (and coherent); DeviceLocal is
/// private to the GPU.
class Buffer final : public phox::Buffer {
public:
    Buffer(id<MTLBuffer> buffer, std::uint64_t size);
    ~Buffer() override;

    id<MTLBuffer> handle() const { return m_buffer; }
    void *mapped() const override;
    /// Shared buffers are coherent, so there is nothing to flush or invalidate.
    void flush(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) override;
    void invalidate(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) override;

private:
    id<MTLBuffer> m_buffer;
};

} // namespace phox::mtl

#endif // PHOX_METAL_BUFFER_HPP
