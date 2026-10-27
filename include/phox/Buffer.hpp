#ifndef PHOX_BUFFER_HPP
#define PHOX_BUFFER_HPP

#include <cstdint>

namespace phox {

class Buffer {
public:
    explicit Buffer(std::uint64_t size) : m_size(size) {}
    virtual ~Buffer() = default;
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    std::uint64_t size() const { return m_size; }
    /// Persistently mapped pointer for Upload and Readback memory; nullptr for DeviceLocal.
    virtual void *mapped() const = 0;
    /// Makes CPU writes to [offset, offset + size) visible to the GPU. A no-op on coherent memory.
    virtual void flush(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) = 0;
    /// Makes GPU writes visible to the CPU before mapped() is read. A no-op on coherent memory.
    virtual void invalidate(std::uint64_t offset = 0, std::uint64_t size = UINT64_MAX) = 0;

protected:
    std::uint64_t m_size;
};

} // namespace phox

#endif // PHOX_BUFFER_HPP
