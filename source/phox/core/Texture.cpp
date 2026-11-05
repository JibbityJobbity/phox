#include "phox/Texture.hpp"

#include "phox/Color.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace phox {

namespace {

std::uint64_t nextTextureId() {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1);
}

const std::array<float, 256> &srgbTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = srgbToLinear(static_cast<float>(i) / 255.0f);
        }
        return result;
    }();
    return table;
}

std::uint8_t toUnorm8(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Texel @p i (wrapping or clamping) along an axis of @p size texels.
std::int64_t wrapIndex(std::int64_t i, std::int64_t size, TextureWrap wrap) {
    switch (wrap) {
    case TextureWrap::Repeat:
        return ((i % size) + size) % size;
    case TextureWrap::MirroredRepeat: {
        const std::int64_t period = 2 * size;
        const std::int64_t m = ((i % period) + period) % period;
        return m < size ? m : period - 1 - m;
    }
    case TextureWrap::ClampToEdge:
        return std::clamp<std::int64_t>(i, 0, size - 1);
    }
    return 0;
}

} // namespace

std::uint32_t mipCountFor(std::uint32_t width, std::uint32_t height) {
    std::uint32_t count = 1;
    for (std::uint32_t size = std::max(width, height); size > 1; size /= 2) ++count;
    return count;
}

Texture::Texture(std::string name, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> rgba,
                 TextureEncoding encoding, SamplerDesc sampler, bool mipmaps)
    : m_id(nextTextureId()), m_name(std::move(name)), m_encoding(encoding), m_sampler(sampler) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("texture '" + m_name + "' has no texels");
    }
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        throw std::invalid_argument("texture '" + m_name + "': expected " + std::to_string(std::size_t{width} * height * 4) +
                                    " bytes of RGBA, got " + std::to_string(rgba.size()));
    }
    m_mips.push_back({width, height, std::move(rgba)});
    if (mipmaps) {
        buildMips();
    }
}

Texture::Texture(const Texture &other)
    : m_id(nextTextureId()), m_name(other.m_name), m_encoding(other.m_encoding), m_sampler(other.m_sampler),
      m_mips(other.m_mips) {}

std::size_t Texture::byteSize() const {
    std::size_t total = 0;
    for (const Mip &mip : m_mips) total += mip.rgba.size();
    return total;
}

void Texture::buildMips() {
    const auto &table = srgbTable();
    while (m_mips.back().width > 1 || m_mips.back().height > 1) {
        const Mip &src = m_mips.back();
        Mip dst;
        dst.width = std::max(1u, src.width / 2);
        dst.height = std::max(1u, src.height / 2);
        dst.rgba.resize(static_cast<std::size_t>(dst.width) * dst.height * 4);
        for (std::uint32_t y = 0; y < dst.height; ++y) {
            for (std::uint32_t x = 0; x < dst.width; ++x) {
                // A 2 x 2 box; an axis of size 1 contributes the same texel twice.
                const std::uint32_t xs[2] = {std::min(2 * x, src.width - 1), std::min(2 * x + 1, src.width - 1)};
                const std::uint32_t ys[2] = {std::min(2 * y, src.height - 1), std::min(2 * y + 1, src.height - 1)};
                vec4 sum{0.0f};
                for (std::uint32_t sy : ys) {
                    for (std::uint32_t sx : xs) {
                        const std::uint8_t *t = &src.rgba[(static_cast<std::size_t>(sy) * src.width + sx) * 4];
                        if (m_encoding == TextureEncoding::Srgb) {
                            sum += vec4(table[t[0]], table[t[1]], table[t[2]], t[3] / 255.0f);
                        } else {
                            sum += vec4(t[0], t[1], t[2], t[3]) / 255.0f;
                        }
                    }
                }
                vec4 texel = sum * 0.25f;
                std::uint8_t *out = &dst.rgba[(static_cast<std::size_t>(y) * dst.width + x) * 4];
                if (m_encoding == TextureEncoding::Srgb) {
                    texel = vec4(linearToSrgb(vec3(texel)), texel.a);
                } else if (m_encoding == TextureEncoding::Normal) {
                    // Averaged normals are shorter than unit; renormalise so lighting does not darken with distance.
                    vec3 n = vec3(texel) * 2.0f - 1.0f;
                    const float length = glm::length(n);
                    n = length > 1e-6f ? n / length : vec3(0.0f, 0.0f, 1.0f);
                    texel = vec4(n * 0.5f + 0.5f, texel.a);
                }
                for (int c = 0; c < 4; ++c) out[c] = toUnorm8(texel[c]);
            }
        }
        m_mips.push_back(std::move(dst));
    }
}

vec4 Texture::sample(vec2 uv) const {
    const Mip &mip = m_mips.front();
    const auto &table = srgbTable();
    const auto w = static_cast<std::int64_t>(mip.width);
    const auto h = static_cast<std::int64_t>(mip.height);
    const auto texel = [&](std::int64_t x, std::int64_t y) {
        x = wrapIndex(x, w, m_sampler.wrapU);
        y = wrapIndex(y, h, m_sampler.wrapV);
        const std::uint8_t *t = &mip.rgba[(static_cast<std::size_t>(y) * mip.width + static_cast<std::size_t>(x)) * 4];
        if (m_encoding == TextureEncoding::Srgb) {
            return vec4(table[t[0]], table[t[1]], table[t[2]], t[3] / 255.0f);
        }
        return vec4(t[0], t[1], t[2], t[3]) / 255.0f;
    };
    const float fx = uv.x * static_cast<float>(w);
    const float fy = uv.y * static_cast<float>(h);
    if (m_sampler.magFilter == TextureFilter::Nearest) {
        return texel(static_cast<std::int64_t>(std::floor(fx)), static_cast<std::int64_t>(std::floor(fy)));
    }
    // Texel centres sit at half-integer coordinates.
    const float x = fx - 0.5f;
    const float y = fy - 0.5f;
    const auto x0 = static_cast<std::int64_t>(std::floor(x));
    const auto y0 = static_cast<std::int64_t>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const vec4 top = glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx);
    const vec4 bottom = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx);
    return glm::mix(top, bottom, ty);
}

Texture Texture::checker(std::string name, std::uint32_t size, std::uint32_t cells, vec4 a, vec4 b,
                         TextureEncoding encoding, SamplerDesc sampler) {
    cells = std::max(cells, 1u);
    const auto encode = [encoding](const vec4 &c) {
        const vec4 stored = encoding == TextureEncoding::Srgb ? vec4(linearToSrgb(vec3(c)), c.a) : c;
        return std::array<std::uint8_t, 4>{toUnorm8(stored.r), toUnorm8(stored.g), toUnorm8(stored.b), toUnorm8(stored.a)};
    };
    const auto first = encode(a);
    const auto second = encode(b);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const bool odd = ((x * cells / size) + (y * cells / size)) % 2 != 0;
            const auto &c = odd ? second : first;
            std::copy(c.begin(), c.end(), rgba.begin() + static_cast<std::ptrdiff_t>((std::size_t{y} * size + x) * 4));
        }
    }
    return Texture(std::move(name), size, size, std::move(rgba), encoding, sampler);
}

} // namespace phox
