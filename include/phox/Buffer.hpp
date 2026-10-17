#ifndef PHOX_BUFFER_HPP
#define PHOX_BUFFER_HPP

#include "phox/Common.hpp"

namespace phox {

class Buffer {
  protected:
    size_t m_size;

  public:
    Buffer(const size_t size);
    size_t getSize() const;
    virtual void *map() = 0;
    virtual void unmap() = 0;
    virtual void free() = 0;
    virtual ~Buffer() = default;
};

}

#endif // PHOX_BUFFER_HPP
