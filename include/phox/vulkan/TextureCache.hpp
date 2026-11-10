#ifndef PHOX_VULKAN_TEXTURE_CACHE_HPP
#define PHOX_VULKAN_TEXTURE_CACHE_HPP

#include "phox/Texture.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

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

    /// Marks @p texture as used this frame (so it is not evicted) and returns its slot. A texture that is
    /// not resident gets a slot now and is uploaded by flush(); @p texture must live until then.
    /// @throws std::runtime_error if the table is full
    std::uint32_t require(const Texture &texture, std::uint64_t frameNumber);
    /// Uploads what require() queued, recording the copies into @p cmd (staged in the frame's upload
    /// arena), and writes their slots. Afterwards the images are ready for fragment shaders.
    void flush(FrameContext &frame, VkCommandBuffer cmd);
    /// The slot of @p texture, which require() has made resident.
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
    std::vector<std::pair<const Texture *, std::uint64_t>> m_pending; ///< (texture, its id) for flush()
};

} // namespace phox::vk

#endif // PHOX_VULKAN_TEXTURE_CACHE_HPP
