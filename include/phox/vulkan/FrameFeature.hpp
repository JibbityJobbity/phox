#ifndef PHOX_VULKAN_FRAME_FEATURE_HPP
#define PHOX_VULKAN_FRAME_FEATURE_HPP

#include "phox/Math.hpp"
#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace phox::vk {

class Device;
class FrameContext;
class GraphExecutor;
class PipelineCache;

/// Triangles made on the GPU during the frame, drawn by the forward pass after the scene's opaque items
/// ("forward generated"), lit and shaded like them. Unindexed: every three vertices make a triangle, and
/// the number of vertices comes from an indirect command the GPU wrote, so a generator decides on the GPU
/// how much it draws.
///
/// Or a heightfield, drawn with task and mesh shaders straight from its heights (@ref heightfield): then
/// @ref vertices and @ref indirect are unused. Only when FrameFeatureContext::meshShaders is set.
struct GeneratedDraw {
    BufferHandle vertices; ///< phox::Vertex[] (48 bytes each), read as BufferUsage::Vertex
    BufferHandle indirect; ///< a VkDrawIndirectCommand at offset 0, read as BufferUsage::Indirect
    mat4 model{1.0f};
    /// Opaque only, and without textures (the texture table holds the scene's textures).
    Material material;

    /// A square grid of heights the mesh shaders turn into a surface (shaders/forward_grid.mesh). The grid
    /// is cut into tiles, one task shader workgroup each: tiles outside the view are dropped (frustum
    /// culling, as MeshletSettings::culling says) and tiles far away use every 2nd or 4th grid point
    /// (MeshletSettings::lodErrorPixels: cells the surface would merge are at most that many pixels wide),
    /// without cracks between tiles of different detail. Normals are central differences of the heights.
    struct Heightfield {
        /// float[resolution * resolution], row by row (x fastest), the surface's height (y) above its
        /// plane at each grid point. Read as BufferUsage::StorageRead by the task and mesh shaders.
        BufferHandle heights;
        /// The heights' device address (the buffer was created with BufferCreateInfo::deviceAddress).
        std::uint64_t heightsAddress = 0;
        std::uint32_t resolution = 0; ///< grid points a side, at least 2
        float size = 1.0f;            ///< world units a side, centred on x = z = 0 before @ref model
    };
    std::optional<Heightfield> heightfield{};
};

/// What a FrameFeature gets each frame to add its passes.
struct FrameFeatureContext {
    RenderGraph &graph;       ///< async compute is set on it when the device has a compute queue
    GraphExecutor &executor;  ///< to bind buffers or images the feature imports
    Device &device;
    PipelineCache &pipelines;
    FrameContext &frame;      ///< per-frame memory and descriptors
    const RenderView &view;
    std::uint64_t frameNumber;
    /// The forward pass draws with task and mesh shaders this frame: GeneratedDraw::heightfield may be used.
    /// Otherwise those draws must give vertices.
    bool meshShaders = false;
};

/// Work an application adds to every frame of a vk::Renderer (vk::Renderer::addFeature), ahead of the
/// forward pass: typically compute passes, marked async() to run on the compute queue, that produce
/// GeneratedDraws. Called on the render thread only.
class FrameFeature {
public:
    virtual ~FrameFeature() = default;
    /// Where the feature's compiled shaders are, searched after the renderer's own.
    virtual std::vector<std::filesystem::path> shaderDirectories() const { return {}; }
    /// The device exists (or was recreated): create persistent resources and pipelines.
    virtual void initialize(Device &, PipelineCache &) {}
    /// The device is idle and about to be destroyed: free everything initialize() made.
    virtual void shutdown() {}
    /// Adds this frame's passes to the graph, and the geometry they produce to @p draws.
    virtual void addPasses(FrameFeatureContext &context, std::vector<GeneratedDraw> &draws) = 0;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_FRAME_FEATURE_HPP
