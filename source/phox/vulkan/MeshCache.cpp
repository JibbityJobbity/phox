#include "phox/vulkan/MeshCache.hpp"

#include "phox/Log.hpp"
#include "phox/Meshlets.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace phox::vk {

MeshCache::MeshCache(Device &device, bool meshlets, bool lod, bool keepMeshlets)
    : m_device(device), m_meshlets(meshlets), m_lod(meshlets && lod), m_keepMeshlets(keepMeshlets) {}

MeshCache::~MeshCache() = default;

VkBuffer MeshCache::indexPool() const { return m_indexPool ? m_indexPool->handle() : VK_NULL_HANDLE; }

std::uint32_t MeshCache::allocateIndices(std::uint32_t count, FrameContext &frame, VkCommandBuffer cmd) {
    // First fit among the ranges given back, else from the end of the pool.
    for (auto it = m_freeIndices.begin(); it != m_freeIndices.end(); ++it) {
        if (it->second >= count) {
            const std::uint32_t first = it->first;
            it->first += count;
            it->second -= count;
            if (it->second == 0) {
                m_freeIndices.erase(it);
            }
            return first;
        }
    }
    const std::uint64_t capacity = m_indexPool ? m_indexPool->size() / sizeof(std::uint32_t) : 0;
    if (m_indexUsed + std::uint64_t{count} > capacity) {
        std::uint64_t grown = std::max<std::uint64_t>(capacity * 2, 1u << 16);
        while (grown < m_indexUsed + std::uint64_t{count}) grown *= 2;
        auto pool = m_device.makeBuffer({grown * sizeof(std::uint32_t),
                                         usageBit(BufferUsage::Index) | usageBit(BufferUsage::TransferDst) |
                                             usageBit(BufferUsage::TransferSrc),
                                         MemoryLocation::DeviceLocal, "index pool"});
        if (m_indexPool && m_indexUsed > 0) {
            // Copies into the old pool may already be recorded in this command buffer.
            VkMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &barrier;
            m_device.table().vkCmdPipelineBarrier2(cmd, &dependency);
            const VkBufferCopy copy{0, 0, VkDeviceSize{m_indexUsed} * sizeof(std::uint32_t)};
            m_device.table().vkCmdCopyBuffer(cmd, m_indexPool->handle(), pool->handle(), 1, &copy);
        }
        if (m_indexPool) {
            std::shared_ptr<Buffer> old(std::move(m_indexPool));
            frame.deferred().push([old]() mutable { old.reset(); });
        }
        m_indexPool = std::move(pool);
        log::debug("mesh cache: index pool grown to {} indices", grown);
    }
    const std::uint32_t first = m_indexUsed;
    m_indexUsed += count;
    return first;
}

void MeshCache::releaseIndices(std::uint32_t first, std::uint32_t count) {
    if (count == 0) {
        return;
    }
    m_freeIndices.emplace_back(first, count);
    // Merge neighbours, so the pool does not crumble into ranges too small to use.
    std::sort(m_freeIndices.begin(), m_freeIndices.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
    for (const auto &range : m_freeIndices) {
        if (!merged.empty() && merged.back().first + merged.back().second == range.first) {
            merged.back().second += range.second;
        } else {
            merged.push_back(range);
        }
    }
    m_freeIndices = std::move(merged);
}

void MeshCache::upload(const Mesh &mesh, GpuMesh &entry, FrameContext &frame, VkCommandBuffer cmd) {
    const auto &table = m_device.table();
    const std::string name = "mesh " + std::to_string(mesh.id());
    // One buffer per part, filled from a list of pieces copied one after another.
    struct Piece {
        const void *data;
        VkDeviceSize bytes;
    };
    const auto upload = [&](std::initializer_list<Piece> pieces, BufferUsageMask usage, const char *what) {
        VkDeviceSize total = 0;
        for (const Piece &piece : pieces) total += piece.bytes;
        // Shaders read every part through its address (GpuMesh), on either path.
        auto buffer = m_device.makeBuffer({total, usage | usageBit(BufferUsage::TransferDst),
                                           MemoryLocation::DeviceLocal, name + " " + what, true});
        VkDeviceSize offset = 0;
        for (const Piece &piece : pieces) {
            const UploadArena::Allocation staging = frame.arena().upload(piece.data, piece.bytes);
            const VkBufferCopy copy{staging.offset, offset, piece.bytes};
            table.vkCmdCopyBuffer(cmd, staging.buffer, buffer->handle(), 1, &copy);
            offset += piece.bytes;
        }
        return buffer;
    };
    const std::vector<std::uint32_t> indices = optimizeIndexOrder(mesh);
    if (m_meshlets) {
        const auto start = std::chrono::steady_clock::now();
        MeshletMesh meshlets = m_lod ? buildMeshletLod(mesh, indices) : buildMeshlets(mesh, indices);
        const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        // The data follows the meshlets in the buffer: make its offsets count from the buffer's start.
        const auto dataStart =
            static_cast<std::uint32_t>(meshlets.meshlets.size() * sizeof(Meshlet) / sizeof(std::uint32_t));
        for (Meshlet &meshlet : meshlets.meshlets) {
            meshlet.vertexOffset += dataStart;
            meshlet.triangleOffset += dataStart;
        }
        entry.vertices = upload({{mesh.vertices().data(), sizeof(Vertex) * mesh.vertices().size()}},
                                usageBit(BufferUsage::StorageRead), "vertices");
        entry.meshlets = upload({{meshlets.meshlets.data(), sizeof(Meshlet) * meshlets.meshlets.size()},
                                 {meshlets.data.data(), sizeof(std::uint32_t) * meshlets.data.size()}},
                                usageBit(BufferUsage::StorageRead), "meshlets");
        entry.meshletCount = static_cast<std::uint32_t>(meshlets.meshlets.size());
        entry.lodLevels = meshlets.levels;
        if (m_keepMeshlets) {
            entry.meshletInfo = meshlets.meshlets;
        }
        log::debug("mesh cache: uploaded mesh {} v{} ({} triangles in {} meshlets, {} LOD level(s), {} triangles "
                   "in all; built in {:.1f} ms)",
                   mesh.id(), mesh.version(), mesh.triangleCount(), meshlets.meshlets.size(), meshlets.levels,
                   meshlets.triangleCount(), buildMs);
    } else {
        // The vertex shader fetches its vertex itself (forward.vert), as storage; indices go to the pool.
        entry.vertices = upload({{mesh.vertices().data(), sizeof(Vertex) * mesh.vertices().size()}},
                                usageBit(BufferUsage::StorageRead), "vertices");
        entry.indexCount = static_cast<std::uint32_t>(indices.size());
        entry.firstIndex = allocateIndices(entry.indexCount, frame, cmd);
        const UploadArena::Allocation staging =
            frame.arena().upload(indices.data(), sizeof(std::uint32_t) * indices.size());
        const VkBufferCopy copy{staging.offset, VkDeviceSize{entry.firstIndex} * sizeof(std::uint32_t),
                                sizeof(std::uint32_t) * indices.size()};
        table.vkCmdCopyBuffer(cmd, staging.buffer, m_indexPool->handle(), 1, &copy);
        log::debug("mesh cache: uploaded mesh {} v{} ({} triangles)", mesh.id(), mesh.version(),
                   mesh.triangleCount());
    }
}

void MeshCache::require(const Mesh &mesh, std::uint64_t frameNumber) {
    if (mesh.empty()) {
        return;
    }
    const auto [it, inserted] = m_meshes.try_emplace(mesh.id());
    GpuMesh &entry = it->second;
    entry.lastUsed = frameNumber;
    if (!inserted && entry.version == mesh.version()) {
        return; // resident, or already queued (the version is set when queued)
    }
    entry.version = mesh.version();
    m_pending.push_back(&mesh);
}

void MeshCache::flush(FrameContext &frame, VkCommandBuffer cmd) {
    if (m_pending.empty()) {
        return;
    }
    const auto &table = m_device.table();
    for (const Mesh *mesh : m_pending) {
        GpuMesh &entry = m_meshes.at(mesh->id());
        if (entry.vertices) {
            // An earlier frame may still be drawing from the old buffers and index range.
            auto retired = std::make_shared<GpuMesh>(std::move(entry));
            frame.deferred().push([this, retired, meshlets = m_meshlets]() mutable {
                if (!meshlets) {
                    releaseIndices(retired->firstIndex, retired->indexCount);
                }
                retired.reset();
            });
            entry = GpuMesh{};
            entry.lastUsed = retired->lastUsed;
        }
        entry.version = mesh->version();
        upload(*mesh, entry, frame, cmd);
    }
    m_pending.clear();
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (m_meshlets) {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    } else {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    }
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    table.vkCmdPipelineBarrier2(cmd, &dependency);
}

const MeshCache::GpuMesh &MeshCache::get(const Mesh &mesh) const {
    const auto it = m_meshes.find(mesh.id());
    if (it == m_meshes.end() || !it->second.vertices) {
        throw std::logic_error("mesh " + std::to_string(mesh.id()) + " was not uploaded by MeshCache::flush");
    }
    return it->second;
}

void MeshCache::collect(std::uint64_t frameNumber) {
    for (auto it = m_meshes.begin(); it != m_meshes.end();) {
        if (frameNumber > it->second.lastUsed + kEvictAfterFrames) {
            log::debug("mesh cache: evicted mesh {}", it->first);
            if (!m_meshlets) {
                releaseIndices(it->second.firstIndex, it->second.indexCount); // no frame this old is in flight
            }
            it = m_meshes.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace phox::vk
