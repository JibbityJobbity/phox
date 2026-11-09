#ifndef PHOX_VULKAN_MESH_CACHE_HPP
#define PHOX_VULKAN_MESH_CACHE_HPP

#include "phox/Meshlets.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phox {
class Mesh;
struct RenderView;
} // namespace phox

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;

/// Device-local copies of scene meshes, keyed by Mesh::id() and refreshed when Mesh::version() changes.
/// These buffers stay in one state once uploaded, so they live outside the render graph.
///
/// Every mesh goes through meshoptimizer on upload. For the vertex path its indices are reordered for
/// the vertex cache and stored in one index pool shared by all meshes, so a single indexed indirect draw
/// can draw any of them (each from its own firstIndex); for the meshlet path (task and mesh shaders) it is
/// split into meshlets instead. Shaders read vertices and meshlets through their device addresses
/// (GpuScene puts them in the frame's GpuMesh array).
class MeshCache {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 120;

    struct GpuMesh {
        std::unique_ptr<Buffer> vertices;
        /// meshlet path only: phox::Meshlet[], then MeshletMesh::data. The meshlets' offsets count uints
        /// from the start of the buffer, so shaders read both through one address.
        std::unique_ptr<Buffer> meshlets;
        std::uint32_t indexCount = 0;   ///< vertex path
        std::uint32_t firstIndex = 0;   ///< vertex path: where its indices start in the index pool
        std::uint32_t meshletCount = 0; ///< of every LOD level
        std::uint32_t lodLevels = 1;
        /// A CPU copy of the meshlets, kept only with statistics on (MeshCache's @p keepMeshlets).
        std::vector<Meshlet> meshletInfo;
        std::uint64_t version = 0;
        std::uint64_t lastUsed = 0;
    };

    /// @param meshlets  upload meshlets for task and mesh shaders instead of index buffers. Needs a device
    ///                  with mesh shaders.
    /// @param lod       meshlet path: build each mesh's cluster LOD hierarchy (phox::buildMeshletLod);
    ///                  otherwise only its full-detail meshlets
    /// @param keepMeshlets  keep a CPU copy of each mesh's meshlets in GpuMesh::meshletInfo
    MeshCache(Device &device, bool meshlets, bool lod = false, bool keepMeshlets = false);
    ~MeshCache();
    MeshCache(const MeshCache &) = delete;
    MeshCache &operator=(const MeshCache &) = delete;

    /// Uploads the meshes @p view draws that are new or changed: copies from the frame's upload arena are
    /// recorded into @p cmd, followed by one barrier that makes them visible to vertex input (or to task
    /// and mesh shaders). Call it before the frame graph executes. Replaced buffers are destroyed once the
    /// GPU is done with them.
    /// @throws std::runtime_error if a mesh has more meshlets than one draw can launch
    void prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber);
    /// @throws std::logic_error if prepare() has not uploaded @p mesh
    const GpuMesh &get(const Mesh &mesh) const;
    /// Destroys meshes no frame has drawn for kEvictAfterFrames. Call after the frame's fence wait.
    void collect(std::uint64_t frameNumber);
    std::size_t size() const { return m_meshes.size(); }
    /// Meshes are uploaded as meshlets, for task and mesh shaders.
    bool meshlets() const { return m_meshlets; }
    /// Meshlets come with cluster LOD hierarchies.
    bool lod() const { return m_lod; }
    /// Vertex path: the index buffer every mesh's indices are in. Null before the first upload.
    VkBuffer indexPool() const;

private:
    // Vertex path: a range of @p count indices in the pool, grown (by a copy recorded into @p cmd) if needed.
    std::uint32_t allocateIndices(std::uint32_t count, FrameContext &frame, VkCommandBuffer cmd);
    void releaseIndices(std::uint32_t first, std::uint32_t count);

    Device &m_device;
    bool m_meshlets;
    std::unique_ptr<Buffer> m_indexPool;
    std::uint32_t m_indexUsed = 0; ///< indices handed out from the end of the pool
    std::vector<std::pair<std::uint32_t, std::uint32_t>> m_freeIndices; ///< (first, count) ranges to reuse
    bool m_lod;
    bool m_keepMeshlets;
    std::unordered_map<std::uint64_t, GpuMesh> m_meshes;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_MESH_CACHE_HPP
