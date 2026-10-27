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
class MeshCache {
public:
    static constexpr std::uint64_t kEvictAfterFrames = 120;

    struct GpuMesh {
        std::unique_ptr<Buffer> vertices;
        std::unique_ptr<Buffer> indices;
        std::uint32_t indexCount = 0;
        std::uint64_t version = 0;
        std::uint64_t lastUsed = 0;
    };

    explicit MeshCache(Device &device);
    ~MeshCache();
    MeshCache(const MeshCache &) = delete;
    MeshCache &operator=(const MeshCache &) = delete;

    /// Uploads the meshes @p view draws that are new or changed: copies from the frame's upload arena are
    /// recorded into @p cmd, followed by one barrier that makes them visible to vertex input. Call it
    /// before the frame graph executes. Replaced buffers are destroyed once the GPU is done with them.
    void prepare(const RenderView &view, FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber);
    /// @throws std::logic_error if prepare() has not uploaded @p mesh
    const GpuMesh &get(const Mesh &mesh) const;
    /// Destroys meshes no frame has drawn for kEvictAfterFrames. Call after the frame's fence wait.
    void collect(std::uint64_t frameNumber);
    std::size_t size() const { return m_meshes.size(); }

private:
    Device &m_device;
    std::unordered_map<std::uint64_t, GpuMesh> m_meshes;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_MESH_CACHE_HPP
