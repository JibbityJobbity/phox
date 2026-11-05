#include "phox/metal/Buffer.hpp"

namespace phox::mtl {

Buffer::Buffer(id<MTLBuffer> buffer, std::uint64_t size) : phox::Buffer(size), m_buffer(buffer) {}

Buffer::~Buffer() = default;

void *Buffer::mapped() const {
    return m_buffer.storageMode == MTLStorageModePrivate ? nullptr : m_buffer.contents;
}

void Buffer::flush(std::uint64_t, std::uint64_t) {}

void Buffer::invalidate(std::uint64_t, std::uint64_t) {}

} // namespace phox::mtl
