#include "phox/metal/TextureCache.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/metal/Device.hpp"

#include <stdexcept>
#include <string>

namespace phox::mtl {

namespace {

MTLSamplerMinMagFilter toMtl(TextureFilter filter) {
    return filter == TextureFilter::Nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode toMtl(TextureWrap wrap) {
    switch (wrap) {
    case TextureWrap::Repeat: return MTLSamplerAddressModeRepeat;
    case TextureWrap::MirroredRepeat: return MTLSamplerAddressModeMirrorRepeat;
    case TextureWrap::ClampToEdge: return MTLSamplerAddressModeClampToEdge;
    }
    throw std::invalid_argument("unknown TextureWrap");
}

} // namespace

TextureCache::TextureCache(Device &device) : m_device(device) {
    const std::vector<std::uint8_t> white(4, 255);
    m_placeholder = upload(Texture("placeholder", 1, 1, white, TextureEncoding::Linear, {}, false));
    m_placeholderSampler = sampler(SamplerDesc{});
}

TextureCache::~TextureCache() = default;

id<MTLTexture> TextureCache::upload(const Texture &texture) {
    const MTLPixelFormat format =
        texture.encoding() == TextureEncoding::Srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                         width:texture.width()
                                                                                        height:texture.height()
                                                                                     mipmapped:texture.mipCount() > 1];
    descriptor.mipmapLevelCount = texture.mipCount();
    descriptor.usage = MTLTextureUsageShaderRead;
    // Shared textures need unified memory (Apple silicon); elsewhere managed ones are synchronised by
    // replaceRegion.
    descriptor.storageMode = m_device.handle().hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
    id<MTLTexture> result = [m_device.handle() newTextureWithDescriptor:descriptor];
    if (result == nil) {
        throw std::runtime_error("failed to create texture '" + texture.name() + "'");
    }
    for (std::uint32_t level = 0; level < texture.mipCount(); ++level) {
        const Texture::Mip &mip = texture.mip(level);
        [result replaceRegion:MTLRegionMake2D(0, 0, mip.width, mip.height)
                  mipmapLevel:level
                    withBytes:mip.rgba.data()
                  bytesPerRow:mip.width * 4];
    }
    result.label = [NSString stringWithUTF8String:("texture " + std::to_string(texture.id()) + " " + texture.name()).c_str()];
    return result;
}

id<MTLSamplerState> TextureCache::sampler(const SamplerDesc &desc) {
    for (const auto &[known, state] : m_samplers) {
        if (known == desc) {
            return state;
        }
    }
    MTLSamplerDescriptor *descriptor = [MTLSamplerDescriptor new];
    descriptor.magFilter = toMtl(desc.magFilter);
    descriptor.minFilter = toMtl(desc.minFilter);
    descriptor.mipFilter = desc.mipFilter == TextureFilter::Nearest ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
    descriptor.sAddressMode = toMtl(desc.wrapU);
    descriptor.tAddressMode = toMtl(desc.wrapV);
    id<MTLSamplerState> state = [m_device.handle() newSamplerStateWithDescriptor:descriptor];
    if (state == nil) {
        throw std::runtime_error("failed to create a sampler state");
    }
    m_samplers.emplace_back(desc, state);
    return state;
}

void TextureCache::prepare(const RenderView &view, std::uint64_t frameNumber) {
    const auto use = [&](const DrawItem &draw) {
        for (const Texture *texture : draw.material->textures()) {
            if (texture == nullptr) {
                continue;
            }
            Entry &entry = m_textures[texture->id()];
            entry.lastUsed = frameNumber;
            if (entry.texture == nil) {
                entry.texture = upload(*texture);
                log::debug("texture cache: uploaded texture {} '{}' ({}x{}, {} mips)", texture->id(), texture->name(),
                           texture->width(), texture->height(), texture->mipCount());
            }
        }
    };
    for (const DrawItem &draw : view.draws) use(draw);
    for (const DrawItem &draw : view.transparent) use(draw);
}

id<MTLTexture> TextureCache::texture(const Texture &texture) const {
    const auto it = m_textures.find(texture.id());
    if (it == m_textures.end() || it->second.texture == nil) {
        throw std::logic_error("texture " + std::to_string(texture.id()) + " was not uploaded by TextureCache::prepare");
    }
    return it->second.texture;
}

void TextureCache::collect(std::uint64_t frameNumber) {
    for (auto it = m_textures.begin(); it != m_textures.end();) {
        if (frameNumber > it->second.lastUsed + kEvictAfterFrames) {
            log::debug("texture cache: evicted texture {}", it->first);
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace phox::mtl
