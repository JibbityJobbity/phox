#ifndef PHOX_RENDERTARGET_HPP
#define PHOX_RENDERTARGET_HPP

#include <cstdint>

namespace phox {

class RenderTarget {
public:
    RenderTarget() = default;
    RenderTarget(std::uint32_t width, std::uint32_t height)
        : m_width(width), m_height(height) {}

    void setExtent(std::uint32_t width, std::uint32_t height) {
        m_width = width;
        m_height = height;
    }

    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }

private:
    std::uint32_t m_width = 1280;
    std::uint32_t m_height = 720;
};

} // namespace phox

#endif // PHOX_RENDERTARGET_HPP
