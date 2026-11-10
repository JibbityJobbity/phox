#ifndef PHOX_VULKAN_GPU_SCENE_HPP
#define PHOX_VULKAN_GPU_SCENE_HPP

#include "phox/Math.hpp"
#include "phox/Scene.hpp"
#include "phox/vulkan/ShaderInterface.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace phox {
struct RenderView;
} // namespace phox

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;
class MeshCache;
class TextureCache;

/// The scene as the GPU-driven forward pass reads it, kept on the GPU across frames: a GpuInstance and a
/// MaterialData per object, indexed by the object's slot, in device-local buffers. Each frame update()
/// finds what changed and hands only that to the scene update pass, which scatters it into the buffers.
/// It reads the objects the scene logged (Scene::changes), and of each, what changed
/// (Object::transformRevision and materialRevision); only when the scene cannot say (the first frame,
/// another scene, a log that started over) does it look at every object. So a still scene costs next to
/// nothing, and a moving one what moves. Meshes whose version changed are found by checking each distinct
/// mesh. The cull passes take it from there. Only the mesh table (a GpuMesh per distinct mesh, which is
/// small) is written every frame.
///
/// An object's slot is its ObjectId::index, which stays the same while the object exists, so the per-slot
/// state the GPU keeps across frames follows it: whether it was visible at the end of the last frame (the
/// history), and with meshlets, one visibility bit per meshlet in whole 32-bit words. Both are hints:
/// stale ones cost time, never correctness, since the late pass tests whatever the early pass skipped. So
/// when a slot gets another object (told apart by Object::id), its words are replaced and its history
/// simply carries over; when the bits run out they start over, cleared.
///
/// Blended objects are not culled on the GPU, because they must be drawn back to front: the CPU writes
/// their GpuDraws and work in that order every frame, in runs that share a pipeline.
///
/// GpuScene holds a reference to every mesh and texture its objects use, and keeps them resident in the
/// caches (MeshCache::require, TextureCache::require) while they are in use.
class GpuScene {
public:
    static constexpr std::uint32_t kInitialVisibilityWords = 1u << 14;
    static constexpr std::uint32_t kInitialSlots = 1u << 10;

    /// A stretch of blended draws that share a cull mode, drawn by one indirect call.
    struct BlendRun {
        bool cullBackFaces = false;
        VkBuffer buffer = VK_NULL_HANDLE; ///< holding the arguments below
        /// Meshlet path: a DrawCounters whose bucket 0 holds the run's work and dispatch arguments. Vertex
        /// path: the run's VkDrawIndexedIndirectCommands.
        VkDeviceSize argsOffset = 0;
        VkDeviceAddress counters = 0; ///< meshlet path: the DrawCounters' address
        VkDeviceAddress work = 0;     ///< meshlet path: the run's work items
        std::uint32_t drawCount = 0;  ///< vertex path: draw commands
    };

    struct Frame {
        VkDeviceAddress meshes = 0;      ///< GpuMesh[], this frame's mesh table
        std::uint32_t instanceCount = 0; ///< slots, empty ones included
        /// Most draws a cull pass can append (objects the GPU culls), at least 1.
        std::uint32_t drawCapacity = 1;
        /// Most work a cull pass can append to one bucket, at least 1: work items (meshlet path), or
        /// indexed draw commands (vertex path).
        std::uint32_t workCapacity = 1;
        VkBuffer indexPool = VK_NULL_HANDLE; ///< vertex path: MeshCache::indexPool(), bound for every draw
        /// Vertex path: draws take the cull pass's count (Device::drawIndirectCount). Otherwise the
        /// command buffer is cleared first and drawn whole, its unused commands drawing nothing.
        bool drawCount = true;
        /// The history and visibility buffers hold nothing usable (new, regrown or restarted): clear them.
        bool clearState = false;
        /// The instance buffer is new: fill it with kNoMesh before the updates, so empty slots are skipped.
        bool clearInstances = false;
        /// What changed since the last frame: InstanceUpdate[] and MaterialUpdate[] for the scene update pass.
        VkDeviceAddress instanceUpdates = 0;
        VkDeviceAddress materialUpdates = 0;
        std::uint32_t instanceUpdateCount = 0;
        std::uint32_t materialUpdateCount = 0;
        VkDeviceAddress blendDraws = 0; ///< GpuDraw[] of blended objects, farthest first
        std::vector<BlendRun> blendRuns;
    };

    /// @param meshlets  the mesh cache holds meshlets (task and mesh shaders); otherwise index buffers
    /// @param occlusion  keep the history and visibility bits for two-phase occlusion culling
    GpuScene(Device &device, bool meshlets, bool occlusion);
    ~GpuScene();
    GpuScene(const GpuScene &) = delete;
    GpuScene &operator=(const GpuScene &) = delete;

    /// Brings the GPU's copy of @p scene up to date: finds what changed, makes the meshes and textures in
    /// use resident (their uploads are recorded into @p cmd, which the frame graph must follow), and
    /// returns this frame's updates and mesh table, in the frame's upload arena. @p view supplies the
    /// camera, for sorting blended objects. Replaced buffers are destroyed once @p frame has finished.
    Frame update(const Scene &scene, const RenderView &view, MeshCache &meshes, TextureCache &textures,
                 FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber);

    /// GpuInstance per slot. Valid after the first update().
    Buffer &instances() const { return *m_instances; }
    /// MaterialData per slot. Valid after the first update().
    Buffer &materials() const { return *m_materials; }
    /// Per slot: visible at the end of the previous frame (uint). Occlusion culling only.
    Buffer *history() const { return m_history.get(); }
    /// Meshlet visibility words. Occlusion culling only.
    Buffer *visibility() const { return m_visibility.get(); }

private:
    /// What update() checks for every object every frame, kept apart from Slot so a still scene's scan
    /// touches 32 bytes per object.
    struct Tracked {
        std::uint64_t object = 0; ///< Object::id of the object in the slot; 0: empty
        std::uint64_t transformRevision = 0;
        std::uint64_t materialRevision = 0;
        std::uint64_t seen = 0; ///< the last update() that found the object
    };
    /// The slot's material, apart from Slot since only material changes touch it.
    struct SlotMaterial {
        MaterialData data;
        std::uint64_t textures[5]{}; ///< Texture::id of each texture the material uses; 0: none
    };
    struct Slot {
        bool occupied = false;
        mat4 model{1.0f};
        std::uint32_t mesh = kNoMesh; ///< index into m_meshes; kNoMesh: the object has none
        /// The mesh (id and version) the visibility words were handed out for.
        std::uint64_t visibilityMesh = 0;
        std::uint64_t visibilityMeshVersion = 0;
        std::uint32_t visibilityOffset = 0;
        std::uint32_t groups = 0;   ///< while counted: the work it adds to a cull pass, and its visibility words
        bool counted = false;       ///< culled on the GPU: part of m_drawCount and m_workTotal
        bool blended = false;       ///< AlphaMode::Blend: drawn by the CPU, back to front
        bool doubleSided = true;
        bool instanceDirty = false; ///< in m_instanceDirty
        bool materialDirty = false; ///< in m_materialDirty
    };
    struct MeshEntry {
        std::shared_ptr<const Mesh> mesh;
        std::uint32_t users = 0;   ///< slots using it
        std::uint64_t version = 0; ///< the Mesh::version `gpu` describes; 0: not built yet
        GpuMesh gpu;
    };
    struct TextureEntry {
        std::shared_ptr<const Texture> texture;
        std::uint32_t users = 0; ///< slots whose material uses it
    };

    /// Brings slot @p index up to date with @p object, if anything changed.
    void track(std::uint32_t index, const Object &object, TextureCache &textures, std::uint64_t frameNumber);
    std::uint32_t acquireMesh(const std::shared_ptr<const Mesh> &mesh);
    void releaseMesh(std::uint32_t index);
    void releaseTextures(SlotMaterial &material);
    /// Empties slot @p index, releasing what it holds; its instance is rewritten as empty.
    void release(std::uint32_t index);
    void markInstance(std::uint32_t index);
    void markMaterial(std::uint32_t index);
    /// Counts @p slot towards the cull passes' capacities with @p groups of work, or stops counting it.
    void setCounted(Slot &slot, bool counted, std::uint32_t groups);

    Device &m_device;
    bool m_meshlets;
    bool m_occlusion;
    Scene::ChangeCursor m_cursor;
    std::uint64_t m_updates = 0; ///< update() calls so far, stamping Tracked::seen
    std::vector<Tracked> m_tracked; ///< by ObjectId::index
    std::vector<Slot> m_slots;      ///< by ObjectId::index
    std::vector<SlotMaterial> m_slotMaterials; ///< by ObjectId::index
    std::uint32_t m_occupied = 0;
    std::vector<std::uint32_t> m_instanceDirty; ///< slots whose instance this update rewrites
    std::vector<std::uint32_t> m_materialDirty; ///< slots whose material this update rewrites
    std::uint32_t m_drawCount = 0; ///< counted slots
    std::uint32_t m_workTotal = 0; ///< their groups
    std::vector<MeshEntry> m_meshes;
    std::vector<std::uint32_t> m_freeMeshes;
    std::unordered_map<std::uint64_t, std::uint32_t> m_meshIndex; ///< by Mesh::id
    std::unordered_map<std::uint64_t, TextureEntry> m_textures;    ///< by Texture::id
    std::vector<std::uint32_t> m_blended; ///< blended slots with a mesh, rebuilt when m_blendedChanged
    bool m_blendedChanged = false;
    std::unique_ptr<Buffer> m_instances;
    std::unique_ptr<Buffer> m_materials;
    std::unique_ptr<Buffer> m_history;
    std::unique_ptr<Buffer> m_visibility;
    std::uint32_t m_visibilityUsed = 0; ///< words handed out since the bits were last cleared
    bool m_stateUndefined = true;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_GPU_SCENE_HPP
