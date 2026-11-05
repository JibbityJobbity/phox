#ifndef PHOX_METAL_TEXTURE_CACHE_HPP
#define PHOX_METAL_TEXTURE_CACHE_HPP

#include "phox/Texture.hpp"
#include "phox/metal/MetalCommon.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phox {
struct RenderView;
}

namespace phox::mtl {

class Device;

/// GPU copies of the textures scene materials use, keyed by Texture::id(), and a sampler state for each
/// distinct SamplerDesc. Textures are immutable, so each is uploaded once (filled directly from the CPU,
/// like MeshCache's buffers) and kept while frames use it.
class TextureCache {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 120;

    explicit TextureCache(Device &device);
    ~TextureCache();
    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;

    /// Uploads every texture the materials of @p view's draws use that is not resident yet.
    void prepare(const RenderView &view, std::uint64_t frameNumber);
    /// @throws std::logic_error if prepare() has not uploaded @p texture
    id<MTLTexture> texture(const Texture &texture) const;
    id<MTLSamplerState> sampler(const SamplerDesc &desc);
    /// A 1 x 1 white texture and a default sampler, bound where a material has no texture: Metal's API
    /// validation wants every texture a shader declares bound, sampled or not.
    id<MTLTexture> placeholder() const { return m_placeholder; }
    id<MTLSamplerState> placeholderSampler() const { return m_placeholderSampler; }
    /// Drops textures no frame has used for kEvictAfterFrames (command buffers in flight keep theirs alive).
    void collect(std::uint64_t frameNumber);
    std::size_t size() const { return m_textures.size(); }

private:
    struct Entry {
        id<MTLTexture> texture;
        std::uint64_t lastUsed = 0;
    };

    id<MTLTexture> upload(const Texture &texture);

    Device &m_device;
    std::unordered_map<std::uint64_t, Entry> m_textures;
    std::vector<std::pair<SamplerDesc, id<MTLSamplerState>>> m_samplers;
    id<MTLTexture> m_placeholder;
    id<MTLSamplerState> m_placeholderSampler;
};

} // namespace phox::mtl

#endif // PHOX_METAL_TEXTURE_CACHE_HPP
