#include "phox/Buffer.hpp"

namespace phox {

Buffer::Buffer(const size_t size) : m_size(size) {}

size_t Buffer::getSize() const { return m_size; }

}
