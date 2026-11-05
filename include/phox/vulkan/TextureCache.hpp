#ifndef PHOX_VULKAN_TEXTURE_CACHE_HPP
#define PHOX_VULKAN_TEXTURE_CACHE_HPP

#include "phox/Texture.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phox {
struct RenderView;
}

namespace phox::vk {

class Device;
class FrameContext;
class Image;
class PipelineCache;

/// GPU copies of the textures scene materials use, keyed by Texture::id(), and the texture table
/// (SetLayout::Bindless) that shaders index: each resident texture has a slot there, with a sampler made
/// from its SamplerDesc. Slots are written as textures arrive, while earlier frames may still be reading
/// other slots (update-after-bind).
class TextureCache {
public:
    /// Same as MeshCache: longer than any frame stays in flight, so evicted slots are never in use.
    static constexpr std::uint64_t kEvictAfterFrames = 120;

    TextureCache(Device &device, const PipelineCache &pipelines);
    ~TextureCache();
    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;

    /// Uploads every texture the materials of @p view's draws use that is not resident yet, recording the
    /// copies into @p cmd (staged in the frame's upload arena), and writes their slots. Afterwards the
    /// images are ready for fragment shaders.
    /// @throws std::runtime_error if the table is full
    void prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber);
    /// The slot of @p texture, which prepare() has made resident.
    /// @throws std::logic_error if it is not
    std::uint32_t slot(const Texture &texture) const;
    /// The texture table, for binding as set SetLayout::Bindless.
    VkDescriptorSet set() const { return m_set; }
    /// Destroys textures no frame has used for kEvictAfterFrames and frees their slots.
    void collect(std::uint64_t frameNumber);
    std::size_t size() const { return m_textures.size(); }

private:
    struct Entry {
        std::unique_ptr<Image> image;
        std::uint32_t slot = 0;
        std::uint64_t lastUsed = 0;
    };

    void upload(const Texture &texture, Entry &entry, FrameContext &frame, VkCommandBuffer cmd);
    VkSampler sampler(const SamplerDesc &desc);

    Device &m_device;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    std::uint32_t m_capacity = 0;
    std::uint32_t m_nextSlot = 0;
    std::vector<std::uint32_t> m_freeSlots;
    std::unordered_map<std::uint64_t, Entry> m_textures;
    std::vector<std::pair<SamplerDesc, VkSampler>> m_samplers;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_TEXTURE_CACHE_HPP
