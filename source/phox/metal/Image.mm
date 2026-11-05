#include "phox/metal/Image.hpp"

namespace phox::mtl {

Image::Image(const ImageCreateInfo &info, id<MTLTexture> texture) : phox::Image(info), m_texture(texture) {}

Image::~Image() = default;

} // namespace phox::mtl
