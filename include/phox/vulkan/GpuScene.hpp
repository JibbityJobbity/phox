#ifndef PHOX_VULKAN_GPU_SCENE_HPP
#define PHOX_VULKAN_GPU_SCENE_HPP

#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace phox {
struct RenderView;
}

namespace phox::vk {

class Buffer;
class Device;
class FrameContext;
class MeshCache;
class TextureCache;

/// The scene as the GPU-driven forward pass reads it. Every frame the CPU writes three arrays to the
/// frame's upload arena, with no culling or sorting: a GpuInstance and a MaterialData per object, indexed
/// by the object's slot, and a GpuMesh per mesh. The cull passes on the GPU take it from there.
///
/// An object's slot is its index in the scene (DrawItem::sceneIndex), which is dense and stays the same
/// while the object exists, so the per-slot state the GPU keeps across frames follows it: whether it was
/// visible at the end of the last frame (the history), and with meshlets, one visibility bit per meshlet in
/// whole 32-bit words. Both are hints: stale ones cost time, never correctness, since the late pass tests
/// whatever the early pass skipped. So when a slot gets another object (told apart by Object::id), its
/// words are replaced and its history simply carries over; when the bits run out they start over, cleared.
///
/// Blended objects are not culled on the GPU, because they must be drawn back to front: the CPU writes
/// their GpuDraws and work in that order (from the view's transparent list), in runs that share a
/// pipeline.
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
        VkDeviceAddress instances = 0;   ///< GpuInstance[instanceCount]
        VkDeviceAddress meshes = 0;      ///< GpuMesh[]
        std::uint32_t instanceCount = 0; ///< slots, empty ones included
        /// Most draws a cull pass can append (objects the GPU culls), at least 1.
        std::uint32_t drawCapacity = 1;
        /// Most work a cull pass can append to one bucket, at least 1: work items (meshlet path), or
        /// indexed draw commands (vertex path).
        std::uint32_t workCapacity = 1;
        VkDescriptorBufferInfo materials{}; ///< MaterialData per slot
        VkBuffer indexPool = VK_NULL_HANDLE; ///< vertex path: MeshCache::indexPool(), bound for every draw
        /// Vertex path: draws take the cull pass's count (Device::drawIndirectCount). Otherwise the
        /// command buffer is cleared first and drawn whole, its unused commands drawing nothing.
        bool drawCount = true;
        /// The history and visibility buffers hold nothing usable (new, regrown or restarted): clear them.
        bool clearState = false;
        VkDeviceAddress blendDraws = 0; ///< GpuDraw[] of blended objects, farthest first
        std::vector<BlendRun> blendRuns;
    };

    /// @param meshlets  the mesh cache holds meshlets (task and mesh shaders); otherwise index buffers
    /// @param occlusion  keep the history and visibility bits for two-phase occlusion culling
    GpuScene(Device &device, bool meshlets, bool occlusion);
    ~GpuScene();
    GpuScene(const GpuScene &) = delete;
    GpuScene &operator=(const GpuScene &) = delete;

    /// Writes this frame's arrays for every item of @p view, which must not be culled (extractView with
    /// culling off), and assigns slots and visibility words. The meshes and textures must have been
    /// uploaded (MeshCache::prepare, TextureCache::prepare). Replaced state buffers are destroyed once
    /// @p frame has finished.
    Frame update(const RenderView &view, const MeshCache &meshes, const TextureCache &textures, FrameContext &frame);

    /// Per slot: visible at the end of the previous frame (uint). Occlusion culling only.
    Buffer *history() const { return m_history.get(); }
    /// Meshlet visibility words. Occlusion culling only.
    Buffer *visibility() const { return m_visibility.get(); }

private:
    struct Slot {
        std::uint64_t object = 0; ///< Object::id of the last object in the slot
        std::uint64_t mesh = 0;
        std::uint64_t meshVersion = 0;
        std::uint32_t visibilityOffset = 0;
        std::uint32_t visibilityWords = 0;
    };

    Device &m_device;
    bool m_meshlets;
    bool m_occlusion;
    std::vector<Slot> m_slots; ///< by DrawItem::sceneIndex
    std::unique_ptr<Buffer> m_history;
    std::unique_ptr<Buffer> m_visibility;
    std::uint32_t m_visibilityUsed = 0; ///< words handed out since the bits were last cleared
    bool m_stateUndefined = true;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_GPU_SCENE_HPP
