#ifndef PHOX_SWAPCHAIN_HPP
#define PHOX_SWAPCHAIN_HPP

#include "phox/RenderTarget.hpp"

#include <cstdint>

namespace phox {

class Swapchain : public RenderTarget {
public:
    Swapchain() = default;
    explicit Swapchain(std::uint32_t width, std::uint32_t height)
        : RenderTarget(width, height) {}
};

} // namespace phox

#endif // PHOX_SWAPCHAIN_HPP
