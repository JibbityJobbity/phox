#ifndef PHOX_RESOURCES_HPP
#define PHOX_RESOURCES_HPP

#include <cstdint>

namespace phox {

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend bool operator==(const Extent2D &, const Extent2D &) = default;
};

} // namespace phox

#endif // PHOX_RESOURCES_HPP
