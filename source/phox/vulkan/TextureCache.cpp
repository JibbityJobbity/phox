#include "phox/vulkan/TextureCache.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/Image.hpp"
#include "phox/vulkan/PipelineCache.hpp"

#include <stdexcept>
#include <string>

namespace phox::vk {

namespace {

VkFilter toVk(TextureFilter filter) { return filter == TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

VkSamplerAddressMode toVk(TextureWrap wrap) {
    switch (wrap) {
    case TextureWrap::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureWrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureWrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    throw std::invalid_argument("unknown TextureWrap");
}

VkImageMemoryBarrier2 imageBarrier(VkImage image, std::uint32_t mips, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    return barrier;
}

void pipelineBarrier(const Device &device, VkCommandBuffer cmd, const std::vector<VkImageMemoryBarrier2> &barriers) {
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    device.table().vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

TextureCache::TextureCache(Device &device, const PipelineCache &pipelines)
    : m_device(device), m_capacity(device.bindlessTextureCount()) {
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_capacity};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    const auto &table = device.table();
    if (table.vkCreateDescriptorPool(device.handle(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the texture table's descriptor pool");
    }
    const VkDescriptorSetLayout layout = pipelines.setLayout(SetLayout::Bindless);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    if (table.vkAllocateDescriptorSets(device.handle(), &allocateInfo, &m_set) != VK_SUCCESS) {
        table.vkDestroyDescriptorPool(device.handle(), m_pool, nullptr);
        throw std::runtime_error("failed to allocate the texture table");
    }
    device.setObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(m_set), "texture table");
}

TextureCache::~TextureCache() {
    m_textures.clear();
    const auto &table = m_device.table();
    for (const auto &[desc, sampler] : m_samplers) table.vkDestroySampler(m_device.handle(), sampler, nullptr);
    table.vkDestroyDescriptorPool(m_device.handle(), m_pool, nullptr);
}

VkSampler TextureCache::sampler(const SamplerDesc &desc) {
    for (const auto &[known, sampler] : m_samplers) {
        if (known == desc) {
            return sampler;
        }
    }
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = toVk(desc.magFilter);
    info.minFilter = toVk(desc.minFilter);
    info.mipmapMode =
        desc.mipFilter == TextureFilter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = toVk(desc.wrapU);
    info.addressModeV = toVk(desc.wrapV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    if (m_device.table().vkCreateSampler(m_device.handle(), &info, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create a texture sampler");
    }
    m_samplers.emplace_back(desc, sampler);
    return sampler;
}

void TextureCache::prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber) {
    std::vector<std::pair<const Texture *, Entry *>> uploads;
    const auto use = [&](const DrawItem &draw) {
        for (const Texture *texture : draw.material->textures()) {
            if (texture == nullptr) {
                continue;
            }
            const auto [it, inserted] = m_textures.try_emplace(texture->id());
            it->second.lastUsed = frameNumber;
            if (!inserted) {
                continue;
            }
            if (m_freeSlots.empty() && m_nextSlot == m_capacity) {
                m_textures.erase(it);
                throw std::runtime_error("the texture table is full (" + std::to_string(m_capacity) + " textures)");
            }
            if (!m_freeSlots.empty()) {
                it->second.slot = m_freeSlots.back();
                m_freeSlots.pop_back();
            } else {
                it->second.slot = m_nextSlot++;
            }
            uploads.emplace_back(texture, &it->second);
        }
    };
    for (const DrawItem &draw : view.draws) use(draw);
    for (const DrawItem &draw : view.transparent) use(draw);
    if (uploads.empty()) {
        return;
    }

    std::vector<VkImageMemoryBarrier2> toTransfer;
    for (auto &[texture, entry] : uploads) {
        ImageCreateInfo info;
        info.extent = {texture->width(), texture->height()};
        info.format = texture->encoding() == TextureEncoding::Srgb ? Format::RGBA8Srgb : Format::RGBA8Unorm;
        info.mipLevels = texture->mipCount();
        info.usage = usageBit(ImageUsage::Sampled) | usageBit(ImageUsage::TransferDst);
        info.name = "texture " + std::to_string(texture->id()) + (texture->name().empty() ? "" : " " + texture->name());
        entry->image = m_device.makeImage(info);
        VkImageMemoryBarrier2 barrier = imageBarrier(entry->image->handle(), texture->mipCount(),
                                                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.push_back(barrier);
    }
    pipelineBarrier(m_device, cmd, toTransfer);

    std::vector<VkImageMemoryBarrier2> toShader;
    std::vector<VkDescriptorImageInfo> imageInfos;
    imageInfos.reserve(uploads.size());
    std::vector<VkWriteDescriptorSet> writes;
    for (auto &[texture, entry] : uploads) {
        for (std::uint32_t level = 0; level < texture->mipCount(); ++level) {
            const Texture::Mip &mip = texture->mip(level);
            const UploadArena::Allocation staging = frame.arena().upload(mip.rgba.data(), mip.rgba.size());
            VkBufferImageCopy copy{};
            copy.bufferOffset = staging.offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {mip.width, mip.height, 1};
            m_device.table().vkCmdCopyBufferToImage(cmd, staging.buffer, entry->image->handle(),
                                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        }
        VkImageMemoryBarrier2 barrier =
            imageBarrier(entry->image->handle(), texture->mipCount(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toShader.push_back(barrier);

        imageInfos.push_back({sampler(texture->sampler()), entry->image->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_set;
        write.dstBinding = 0;
        write.dstArrayElement = entry->slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
        log::debug("texture cache: uploaded texture {} '{}' ({}x{}, {} mips) to slot {}", texture->id(),
                   texture->name(), texture->width(), texture->height(), texture->mipCount(), entry->slot);
    }
    pipelineBarrier(m_device, cmd, toShader);
    m_device.table().vkUpdateDescriptorSets(m_device.handle(), static_cast<std::uint32_t>(writes.size()), writes.data(),
                                           0, nullptr);
}

std::uint32_t TextureCache::slot(const Texture &texture) const {
    const auto it = m_textures.find(texture.id());
    if (it == m_textures.end() || !it->second.image) {
        throw std::logic_error("texture " + std::to_string(texture.id()) + " was not uploaded by TextureCache::prepare");
    }
    return it->second.slot;
}

void TextureCache::collect(std::uint64_t frameNumber) {
    for (auto it = m_textures.begin(); it != m_textures.end();) {
        if (frameNumber > it->second.lastUsed + kEvictAfterFrames) {
            log::debug("texture cache: evicted texture {} from slot {}", it->first, it->second.slot);
            // Partially bound: the stale descriptor is never read, and the slot is rewritten on reuse.
            m_freeSlots.push_back(it->second.slot);
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace phox::vk
