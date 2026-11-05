#include "phox/metal/MeshCache.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/metal/Buffer.hpp"
#include "phox/metal/Device.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace phox::mtl {

MeshCache::MeshCache(Device &device) : m_device(device) {}

MeshCache::~MeshCache() = default;

void MeshCache::prepare(const RenderView &view, std::uint64_t frameNumber) {
    for (const DrawItem &draw : view.draws) {
        const Mesh &mesh = *draw.mesh;
        GpuMesh &entry = m_meshes[mesh.id()];
        entry.lastUsed = frameNumber;
        if (entry.vertices && entry.version == mesh.version()) {
            continue;
        }
        // New buffers rather than rewriting the old ones, which an earlier frame may still be drawing from.
        const std::string name = "mesh " + std::to_string(mesh.id());
        const std::uint64_t vertexBytes = sizeof(Vertex) * mesh.vertices().size();
        const std::uint64_t indexBytes = sizeof(std::uint32_t) * mesh.indices().size();
        entry.vertices = m_device.makeBuffer(
            {vertexBytes, usageBit(BufferUsage::Vertex), MemoryLocation::Upload, name + " vertices"});
        entry.indices =
            m_device.makeBuffer({indexBytes, usageBit(BufferUsage::Index), MemoryLocation::Upload, name + " indices"});
        std::memcpy(entry.vertices->mapped(), mesh.vertices().data(), vertexBytes);
        std::memcpy(entry.indices->mapped(), mesh.indices().data(), indexBytes);
        entry.indexCount = static_cast<std::uint32_t>(mesh.indices().size());
        entry.version = mesh.version();
        log::debug("mesh cache: uploaded mesh {} v{} ({} triangles)", mesh.id(), mesh.version(), mesh.triangleCount());
    }
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

} // namespace phox::mtl
