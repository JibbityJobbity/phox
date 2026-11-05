#ifndef PHOX_TEXTURE_HPP
#define PHOX_TEXTURE_HPP

#include "phox/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace phox {

enum class TextureFilter : std::uint8_t { Nearest, Linear };

enum class TextureWrap : std::uint8_t { Repeat, MirroredRepeat, ClampToEdge };

/// How a texture is sampled (glTF's sampler). Backends cache one sampler object per distinct description.
struct SamplerDesc {
    TextureFilter magFilter = TextureFilter::Linear;
    TextureFilter minFilter = TextureFilter::Linear;
    /// Between mips; Nearest picks the closest mip. Only matters for textures with more than one mip.
    TextureFilter mipFilter = TextureFilter::Linear;
    TextureWrap wrapU = TextureWrap::Repeat;
    TextureWrap wrapV = TextureWrap::Repeat;
    friend bool operator==(const SamplerDesc &, const SamplerDesc &) = default;
};

/// What the texels of a texture mean, which decides how they are filtered and averaged into mips.
enum class TextureEncoding : std::uint8_t {
    Srgb,   ///< colours stored with the sRGB transfer function (base colour, emissive); decoded when sampled
    Linear, ///< data (metallic-roughness, occlusion)
    Normal, ///< a tangent-space normal map: xyz = rgb * 2 - 1; mips are renormalised
};

/// An immutable 2D image with 8-bit RGBA texels and a full mip chain, built on the CPU when the texture
/// is made so that every backend (and the software rasterizer) samples the same mips. Each texture has
/// an id that is never reused, so renderers can cache GPU copies of it.
class Texture {
public:
    struct Mip {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba; ///< width * height * 4 bytes, rows top to bottom
    };

    /// @param rgba  width * height * 4 bytes, rows top to bottom (glTF's and most image files' order)
    /// @param mipmaps  build the mip chain down to 1 x 1; otherwise the texture has one mip
    /// @throws std::invalid_argument if the size is zero or @p rgba has the wrong length
    Texture(std::string name, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> rgba,
            TextureEncoding encoding, SamplerDesc sampler = {}, bool mipmaps = true);
    /// A copy is a different texture to caches: it gets its own id.
    Texture(const Texture &other);
    Texture &operator=(const Texture &) = delete;

    std::uint64_t id() const { return m_id; }
    const std::string &name() const { return m_name; }
    std::uint32_t width() const { return m_mips.front().width; }
    std::uint32_t height() const { return m_mips.front().height; }
    TextureEncoding encoding() const { return m_encoding; }
    const SamplerDesc &sampler() const { return m_sampler; }
    std::uint32_t mipCount() const { return static_cast<std::uint32_t>(m_mips.size()); }
    const Mip &mip(std::uint32_t level) const { return m_mips.at(level); }
    std::size_t byteSize() const;

    /// Bilinear sample of mip 0 at @p uv with the sampler's wrap modes, as the GPU would return it: sRGB
    /// texels decoded to linear, the others as stored (in [0, 1]). For the software rasterizer.
    vec4 sample(vec2 uv) const;

    /// A @p size x @p size checkerboard of @p cells x @p cells squares, for samples and tests.
    static Texture checker(std::string name, std::uint32_t size, std::uint32_t cells, vec4 a, vec4 b,
                           TextureEncoding encoding = TextureEncoding::Srgb, SamplerDesc sampler = {});

private:
    void buildMips();

    std::uint64_t m_id;
    std::string m_name;
    TextureEncoding m_encoding;
    SamplerDesc m_sampler;
    std::vector<Mip> m_mips;
};

/// The number of mips of a full chain for a @p width x @p height image (1 x 1 included).
std::uint32_t mipCountFor(std::uint32_t width, std::uint32_t height);

} // namespace phox

#endif // PHOX_TEXTURE_HPP
