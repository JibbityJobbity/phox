#ifndef PHOX_IMAGE_HPP
#define PHOX_IMAGE_HPP

#include "phox/Resources.hpp"

#include <utility>

namespace phox {

class Image {
public:
    explicit Image(ImageCreateInfo info) : m_info(std::move(info)) {}
    virtual ~Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    const ImageCreateInfo &info() const { return m_info; }
    Extent2D extent() const { return m_info.extent; }
    Format format() const { return m_info.format; }

protected:
    ImageCreateInfo m_info;
};

} // namespace phox

#endif // PHOX_IMAGE_HPP
