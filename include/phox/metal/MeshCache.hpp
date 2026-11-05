#ifndef PHOX_METAL_MESH_CACHE_HPP
#define PHOX_METAL_MESH_CACHE_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace phox {
class Mesh;
struct RenderView;
} // namespace phox

namespace phox::mtl {

class Buffer;
class Device;

/// GPU copies of scene meshes, keyed by Mesh::id() and refreshed when Mesh::version() changes.
///
/// The buffers are shared with the CPU and filled directly, which suits Apple silicon's unified memory;
/// no copy or barrier is recorded. A replaced buffer can be dropped at once: command buffers that still
/// draw from it keep it alive.
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

    /// Uploads the meshes @p view draws that are new or changed. Call it before the frame graph executes.
    void prepare(const RenderView &view, std::uint64_t frameNumber);
    /// @throws std::logic_error if prepare() has not uploaded @p mesh
    const GpuMesh &get(const Mesh &mesh) const;
    /// Destroys meshes no frame has drawn for kEvictAfterFrames.
    void collect(std::uint64_t frameNumber);
    std::size_t size() const { return m_meshes.size(); }

private:
    Device &m_device;
    std::unordered_map<std::uint64_t, GpuMesh> m_meshes;
};

} // namespace phox::mtl

#endif // PHOX_METAL_MESH_CACHE_HPP
