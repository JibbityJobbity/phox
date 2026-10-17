#include "VulkanCommandList.hpp"

#include "States.hpp"

namespace phox::vulkan {

VulkanCommandList::VulkanCommandList(VulkanDevice &device, VkCommandBuffer commandBuffer)
    : m_device(device), m_cb(commandBuffer) {}

void VulkanCommandList::end() { PHOX_VK_CHECK(m_device.table().vkEndCommandBuffer(m_cb)); }

const PipelineData &VulkanCommandList::boundPipeline() const {
    if (!m_pipeline.valid()) {
        throw std::logic_error("CommandList: no pipeline is bound");
    }
    return m_device.pipelineData(m_pipeline);
}

void VulkanCommandList::barrier(std::span<const rhi::TextureBarrier> textures, std::span<const rhi::BufferBarrier> buffers) {
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    for (const rhi::TextureBarrier &t : textures) {
        const TextureData &texture = m_device.textureData(t.texture);
        const SyncState src = syncState(t.from, true);
        const SyncState dst = syncState(t.to, false);
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src.stage;
        b.srcAccessMask = src.access;
        b.dstStageMask = dst.stage;
        b.dstAccessMask = dst.access;
        b.oldLayout = src.layout;
        b.newLayout = dst.layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = texture.image;
        b.subresourceRange = {aspectOf(texture.desc.format), 0, texture.desc.mips, 0, 1};
        imageBarriers.push_back(b);
    }
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    for (const rhi::BufferBarrier &t : buffers) {
        const BufferData &buffer = m_device.bufferData(t.buffer);
        const SyncState src = syncState(t.from, true);
        const SyncState dst = syncState(t.to, false);
        VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        b.srcStageMask = src.stage;
        b.srcAccessMask = src.access;
        b.dstStageMask = dst.stage;
        b.dstAccessMask = dst.access;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buffer.buffer;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(b);
    }
    if (imageBarriers.empty() && bufferBarriers.empty()) {
        return;
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependency.pImageMemoryBarriers = imageBarriers.data();
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    dependency.pBufferMemoryBarriers = bufferBarriers.data();
    m_device.table().vkCmdPipelineBarrier2(m_cb, &dependency);
}

void VulkanCommandList::beginRendering(const rhi::RenderingInfo &info) {
    std::vector<VkRenderingAttachmentInfo> colors;
    for (const rhi::ColorTarget &c : info.colors) {
        VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = m_device.textureData(c.texture).view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = c.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        for (size_t i = 0; i < 4; ++i) {
            a.clearValue.color.float32[i] = c.clearColor[i];
        }
        colors.push_back(a);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (info.depth) {
        depth.imageView = m_device.textureData(info.depth->texture).view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = info.depth->clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = {info.depth->clearDepth, 0};
    }

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {info.width, info.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    rendering.pColorAttachments = colors.data();
    rendering.pDepthAttachment = info.depth ? &depth : nullptr;
    m_device.table().vkCmdBeginRendering(m_cb, &rendering);

    // Not flipped: +y in clip space points down the framebuffer.
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(info.width), static_cast<float>(info.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {info.width, info.height}};
    m_device.table().vkCmdSetViewport(m_cb, 0, 1, &viewport);
    m_device.table().vkCmdSetScissor(m_cb, 0, 1, &scissor);
}

void VulkanCommandList::endRendering() { m_device.table().vkCmdEndRendering(m_cb); }

void VulkanCommandList::bindPipeline(rhi::PipelineHandle pipeline) {
    const PipelineData &data = m_device.pipelineData(pipeline);
    m_device.table().vkCmdBindPipeline(m_cb, data.bindPoint, data.pipeline);
    m_pipeline = pipeline;
    // A new pipeline layout can disturb bound sets, so everything pending is bound again at the next draw.
    for (const auto &entry : m_pending) {
        m_dirty.insert(entry.first);
    }
}

void VulkanCommandList::bindVertexBuffer(rhi::BufferHandle buffer, uint64_t offset) {
    const VkBuffer handle = m_device.bufferData(buffer).buffer;
    m_device.table().vkCmdBindVertexBuffers(m_cb, 0, 1, &handle, &offset);
}

void VulkanCommandList::bindIndexBuffer(rhi::BufferHandle buffer, uint64_t offset) {
    m_device.table().vkCmdBindIndexBuffer(m_cb, m_device.bufferData(buffer).buffer, offset, VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::bindUniformBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle buffer) {
    (void)m_device.bufferData(buffer); // validates the handle now rather than at the draw
    m_pending[set][binding] = Pending{buffer, {}};
    m_dirty.insert(set);
}

void VulkanCommandList::bindStorageBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle buffer) {
    (void)m_device.bufferData(buffer);
    m_pending[set][binding] = Pending{buffer, {}};
    m_dirty.insert(set);
}

void VulkanCommandList::bindSampledTexture(uint32_t set, uint32_t binding, rhi::TextureHandle texture) {
    (void)m_device.textureData(texture);
    m_pending[set][binding] = Pending{{}, texture};
    m_dirty.insert(set);
}

void VulkanCommandList::pushConstants(rhi::ShaderStage stage, uint32_t offset, std::span<const std::byte> data) {
    m_device.table().vkCmdPushConstants(m_cb, boundPipeline().layout, toVk(stage), offset,
                                        static_cast<uint32_t>(data.size()), data.data());
}

VkDescriptorSet VulkanCommandList::allocateSet(VkDescriptorSetLayout layout) {
    if (m_pools.empty()) {
        m_pools.push_back(m_device.acquireDescriptorPool());
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = m_pools.back();
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult result = m_device.table().vkAllocateDescriptorSets(m_device.handle(), &info, &set);
        if (result == VK_SUCCESS) {
            return set;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            throwVk("vkAllocateDescriptorSets", result);
        }
        m_pools.push_back(m_device.acquireDescriptorPool());
    }
    throw std::runtime_error("Vulkan: descriptor pool exhausted");
}

void VulkanCommandList::flushDescriptors() {
    if (m_dirty.empty()) {
        return;
    }
    const PipelineData &pipeline = boundPipeline();
    const VolkDeviceTable &table = m_device.table();

    for (const uint32_t set : m_dirty) {
        if (set >= pipeline.setLayouts.size()) {
            continue; // the bound pipeline does not use this set
        }
        const auto pendingSet = m_pending.find(set);

        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;
        size_t declared = 0;
        for (const DescriptorBinding &b : pipeline.bindings) {
            declared += b.set == set ? 1 : 0;
        }
        bufferInfos.reserve(declared); // no reallocation, so the pointers in `writes` stay valid
        imageInfos.reserve(declared);

        for (const DescriptorBinding &b : pipeline.bindings) {
            if (b.set != set) {
                continue;
            }
            const Pending *resource = nullptr;
            if (pendingSet != m_pending.end()) {
                const auto found = pendingSet->second.find(b.binding);
                resource = found != pendingSet->second.end() ? &found->second : nullptr;
            }
            if (resource == nullptr) {
                throw std::logic_error("CommandList: descriptor set " + std::to_string(set) + " binding " +
                                       std::to_string(b.binding) + " is not bound");
            }

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstBinding = b.binding;
            write.descriptorCount = 1;
            switch (b.type) {
            case rhi::BindingType::UniformBuffer:
            case rhi::BindingType::StorageBuffer:
                bufferInfos.push_back({m_device.bufferData(resource->buffer).buffer, 0, VK_WHOLE_SIZE});
                write.descriptorType = b.type == rhi::BindingType::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.pBufferInfo = &bufferInfos.back();
                break;
            case rhi::BindingType::SampledTexture:
                imageInfos.push_back({VK_NULL_HANDLE, m_device.textureData(resource->texture).view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}); // sampler is immutable
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imageInfos.back();
                break;
            case rhi::BindingType::StorageTexture:
                imageInfos.push_back({VK_NULL_HANDLE, m_device.textureData(resource->texture).view, VK_IMAGE_LAYOUT_GENERAL});
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                write.pImageInfo = &imageInfos.back();
                break;
            }
            writes.push_back(write);
        }

        if (pipeline.pushDescriptors) {
            table.vkCmdPushDescriptorSetKHR(m_cb, pipeline.bindPoint, pipeline.layout, set,
                                            static_cast<uint32_t>(writes.size()), writes.data());
        } else {
            const VkDescriptorSet descriptorSet = allocateSet(pipeline.setLayouts[set]);
            for (VkWriteDescriptorSet &w : writes) {
                w.dstSet = descriptorSet;
            }
            table.vkUpdateDescriptorSets(m_device.handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            table.vkCmdBindDescriptorSets(m_cb, pipeline.bindPoint, pipeline.layout, set, 1, &descriptorSet, 0, nullptr);
        }
    }
    m_dirty.clear();
}

void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) {
    flushDescriptors();
    m_device.table().vkCmdDraw(m_cb, vertexCount, instanceCount, firstVertex, 0);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) {
    flushDescriptors();
    m_device.table().vkCmdDrawIndexed(m_cb, indexCount, instanceCount, firstIndex, 0, 0);
}

void VulkanCommandList::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) {
    if (!m_device.capabilities().has(rhi::Feature::MeshShader) || m_device.table().vkCmdDrawMeshTasksEXT == nullptr) {
        assert(false && "drawMeshTasks requires Feature::MeshShader");
        throw std::logic_error("drawMeshTasks requires Feature::MeshShader");
    }
    flushDescriptors();
    m_device.table().vkCmdDrawMeshTasksEXT(m_cb, x, y, z);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    flushDescriptors();
    m_device.table().vkCmdDispatch(m_cb, x, y, z);
}

void VulkanCommandList::copyBuffer(rhi::BufferHandle src, rhi::BufferHandle dst, uint64_t size) {
    const VkBufferCopy region{0, 0, size};
    m_device.table().vkCmdCopyBuffer(m_cb, m_device.bufferData(src).buffer, m_device.bufferData(dst).buffer, 1, &region);
}

void VulkanCommandList::copyTextureToBuffer(rhi::TextureHandle src, rhi::BufferHandle dst) {
    const TextureData &texture = m_device.textureData(src);
    const VkImageAspectFlags aspect =
        (aspectOf(texture.desc.format) & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkBufferImageCopy region{};
    region.imageSubresource = {aspect, 0, 0, 1};
    region.imageExtent = {texture.desc.width, texture.desc.height, 1};
    m_device.table().vkCmdCopyImageToBuffer(m_cb, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            m_device.bufferData(dst).buffer, 1, &region);
}

void VulkanCommandList::beginDebugLabel(const char *name) {
    if (!m_device.debugUtilsEnabled() || vkCmdBeginDebugUtilsLabelEXT == nullptr) {
        return;
    }
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name != nullptr ? name : "";
    vkCmdBeginDebugUtilsLabelEXT(m_cb, &label);
}

void VulkanCommandList::endDebugLabel() {
    if (!m_device.debugUtilsEnabled() || vkCmdEndDebugUtilsLabelEXT == nullptr) {
        return;
    }
    vkCmdEndDebugUtilsLabelEXT(m_cb);
}

} // namespace phox::vulkan
