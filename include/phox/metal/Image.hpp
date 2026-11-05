#ifndef PHOX_METAL_IMAGE_HPP
#define PHOX_METAL_IMAGE_HPP

#include "phox/Image.hpp"
#include "phox/metal/MetalCommon.hpp"

namespace phox::mtl {

/// A GPU-private texture. Unlike Vulkan there is no separate view: the texture is bound directly.
class Image final : public phox::Image {
public:
    Image(const ImageCreateInfo &info, id<MTLTexture> texture);
    ~Image() override;

    id<MTLTexture> handle() const { return m_texture; }

private:
    id<MTLTexture> m_texture;
};

} // namespace phox::mtl

#endif // PHOX_METAL_IMAGE_HPP
