#include "phox/vulkan/MeshCache.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"

#include <stdexcept>
#include <string>

namespace phox::vk {

MeshCache::MeshCache(Device &device) : m_device(device) {}

MeshCache::~MeshCache() = default;

void MeshCache::prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber) {
    const auto &table = m_device.table();
    bool uploaded = false;
    for (const DrawItem &draw : view.draws) {
        const Mesh &mesh = *draw.mesh;
        GpuMesh &entry = m_meshes[mesh.id()];
        entry.lastUsed = frameNumber;
        if (entry.vertices && entry.version == mesh.version()) {
            continue;
        }
        if (entry.vertices) {
            // An earlier frame may still be drawing from the old buffers.
            auto retired = std::make_shared<GpuMesh>(std::move(entry));
            frame.deferred().push([retired]() mutable { retired.reset(); });
            entry = GpuMesh{};
            entry.lastUsed = frameNumber;
        }

        const std::string name = "mesh " + std::to_string(mesh.id());
        const VkDeviceSize vertexBytes = sizeof(Vertex) * mesh.vertices().size();
        const VkDeviceSize indexBytes = sizeof(std::uint32_t) * mesh.indices().size();
        entry.vertices = m_device.makeBuffer({vertexBytes, usageBit(BufferUsage::Vertex) | usageBit(BufferUsage::TransferDst),
                                              MemoryLocation::DeviceLocal, name + " vertices"});
        entry.indices = m_device.makeBuffer({indexBytes, usageBit(BufferUsage::Index) | usageBit(BufferUsage::TransferDst),
                                             MemoryLocation::DeviceLocal, name + " indices"});
        entry.indexCount = static_cast<std::uint32_t>(mesh.indices().size());
        entry.version = mesh.version();

        const UploadArena::Allocation vertexData = frame.arena().upload(mesh.vertices().data(), vertexBytes);
        const UploadArena::Allocation indexData = frame.arena().upload(mesh.indices().data(), indexBytes);
        const VkBufferCopy vertexCopy{vertexData.offset, 0, vertexBytes};
        const VkBufferCopy indexCopy{indexData.offset, 0, indexBytes};
        table.vkCmdCopyBuffer(cmd, vertexData.buffer, entry.vertices->handle(), 1, &vertexCopy);
        table.vkCmdCopyBuffer(cmd, indexData.buffer, entry.indices->handle(), 1, &indexCopy);
        uploaded = true;
        log::debug("mesh cache: uploaded mesh {} v{} ({} triangles)", mesh.id(), mesh.version(), mesh.triangleCount());
    }
    if (!uploaded) {
        return;
    }
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    table.vkCmdPipelineBarrier2(cmd, &dependency);
}

const MeshCache::GpuMesh &MeshCache::get(const Mesh &mesh) const {
    const auto it = m_meshes.find(mesh.id());
    if (it == m_meshes.end() || !it->second.vertices) {
        throw std::logic_error("mesh " + std::to_string(mesh.id()) + " was not uploaded by MeshCache::prepare");
    }
    return it->second;
}

void MeshCache::collect(std::uint64_t frameNumber) {
    for (auto it = m_meshes.begin(); it != m_meshes.end();) {
        if (frameNumber > it->second.lastUsed + kEvictAfterFrames) {
            log::debug("mesh cache: evicted mesh {}", it->first);
            it = m_meshes.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace phox::vk
