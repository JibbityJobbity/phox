#ifndef PHOX_VULKAN_MESH_CACHE_HPP
#define PHOX_VULKAN_MESH_CACHE_HPP

#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

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
/// the vertex cache; for the meshlet path (task and mesh shaders) it is split into meshlets instead,
/// and shaders read vertices and meshlets through buffer device addresses.
class MeshCache {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 120;

    struct GpuMesh {
        std::unique_ptr<Buffer> vertices;
        std::unique_ptr<Buffer> indices;     ///< vertex path only
        /// meshlet path only: phox::Meshlet[], then MeshletMesh::data. The meshlets' offsets count uints
        /// from the start of the buffer, so shaders read both through one address.
        std::unique_ptr<Buffer> meshlets;
        std::uint32_t indexCount = 0;
        std::uint32_t meshletCount = 0;
        std::uint64_t version = 0;
        std::uint64_t lastUsed = 0;
    };

    /// @param meshlets  upload meshlets for task and mesh shaders instead of index buffers. Needs a device
    ///                  with mesh shaders.
    MeshCache(Device &device, bool meshlets);
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

private:
    Device &m_device;
    bool m_meshlets;
    std::unordered_map<std::uint64_t, GpuMesh> m_meshes;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_MESH_CACHE_HPP
