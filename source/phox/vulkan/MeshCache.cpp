#include "phox/vulkan/MeshCache.hpp"

#include "phox/Log.hpp"
#include "phox/Meshlets.hpp"
#include "phox/RenderView.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace phox::vk {

MeshCache::MeshCache(Device &device, bool meshlets, bool lod, bool keepMeshlets)
    : m_device(device), m_meshlets(meshlets), m_lod(meshlets && lod), m_keepMeshlets(keepMeshlets) {}

MeshCache::~MeshCache() = default;

void MeshCache::prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber) {
    const auto &table = m_device.table();
    bool uploaded = false;
    std::vector<const DrawItem *> items;
    items.reserve(view.itemCount());
    for (const DrawItem &draw : view.draws) items.push_back(&draw);
    for (const DrawItem &draw : view.transparent) items.push_back(&draw);
    for (const DrawItem *draw : items) {
        const Mesh &mesh = *draw->mesh;
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
        // One buffer per part, filled from a list of pieces copied one after another.
        struct Piece {
            const void *data;
            VkDeviceSize bytes;
        };
        const auto upload = [&](std::initializer_list<Piece> pieces, BufferUsageMask usage, const char *what) {
            VkDeviceSize total = 0;
            for (const Piece &piece : pieces) total += piece.bytes;
            auto buffer = m_device.makeBuffer({total, usage | usageBit(BufferUsage::TransferDst),
                                               MemoryLocation::DeviceLocal, name + " " + what, m_meshlets});
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
            const std::uint64_t groups = (meshlets.meshlets.size() + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;
            if (groups > m_device.meshShaderProperties().maxTaskWorkGroupCount[0]) {
                throw std::runtime_error(name + " has " + std::to_string(meshlets.meshlets.size()) +
                                         " meshlets, more than one draw can launch");
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
            entry.vertices = upload({{mesh.vertices().data(), sizeof(Vertex) * mesh.vertices().size()}},
                                    usageBit(BufferUsage::Vertex), "vertices");
            entry.indices = upload({{indices.data(), sizeof(std::uint32_t) * indices.size()}},
                                   usageBit(BufferUsage::Index), "indices");
            entry.indexCount = static_cast<std::uint32_t>(indices.size());
            log::debug("mesh cache: uploaded mesh {} v{} ({} triangles)", mesh.id(), mesh.version(),
                       mesh.triangleCount());
        }
        entry.version = mesh.version();
        uploaded = true;
    }
    if (!uploaded) {
        return;
    }
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (m_meshlets) {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    } else {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
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
