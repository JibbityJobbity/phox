#ifndef PHOX_MESHLETS_HPP
#define PHOX_MESHLETS_HPP

#include "phox/Math.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace phox {

class Mesh;

/// Meshlet size limits. 64 vertices and 124 triangles suit both NVIDIA's and AMD's mesh shading
/// hardware; shaders/forward.mesh declares the same maximum outputs.
inline constexpr std::uint32_t kMeshletMaxVertices = 64;
inline constexpr std::uint32_t kMeshletMaxTriangles = 124;

/// A cluster of up to kMeshletMaxTriangles triangles of one mesh, with the bounds that let a task
/// shader cull it as a whole. Laid out as the shaders read it (std430, 64 bytes), so arrays of these
/// upload as they are.
struct Meshlet {
    vec4 sphere{0.0f}; ///< xyz: centre of a sphere enclosing the meshlet (object space); w: its radius
    /// xyz: axis of a cone containing every triangle normal; w: cutoff. The meshlet faces away from a
    /// camera at c (object space) if dot(normalize(coneApex - c), axis) >= cutoff. Above 1 when no cone fits.
    vec4 cone{0.0f, 0.0f, 1.0f, 2.0f};
    vec4 coneApex{0.0f}; ///< xyz; w unused
    std::uint32_t vertexOffset = 0;   ///< first entry of MeshletMesh::data holding this meshlet's vertex indices
    std::uint32_t triangleOffset = 0; ///< first entry of MeshletMesh::data holding its triangles (see there)
    std::uint32_t vertexCount = 0;
    std::uint32_t triangleCount = 0;
};
static_assert(sizeof(Meshlet) == 64, "Meshlet must match the std430 layout in shaders/common.glsl");

/// A mesh split into meshlets. data holds, for each meshlet, vertexCount indices into the mesh's
/// vertices, then triangleCount triangles packed one per entry as three 8-bit indices into that list
/// (bits 0-7, 8-15, 16-23), wound like the mesh's own triangles.
struct MeshletMesh {
    std::vector<Meshlet> meshlets;
    std::vector<std::uint32_t> data;
    std::size_t triangleCount() const;
};

/// @p mesh's indices reordered for the GPU's post-transform vertex cache (meshoptimizer). The same
/// triangles with the same winding, in a different order.
std::vector<std::uint32_t> optimizeIndexOrder(const Mesh &mesh);

/// Splits the triangles in @p indices (of @p mesh's vertices) into meshlets with culling bounds, keeping
/// spatially close triangles together, and orders each meshlet's vertices and triangles for locality.
/// Pass the output of optimizeIndexOrder for the best clusters.
MeshletMesh buildMeshlets(const Mesh &mesh, const std::vector<std::uint32_t> &indices);

/// Whether every triangle of @p meshlet faces away from a camera at @p camera (object space): the task
/// shader's cone test. Conservative: false whenever it cannot tell.
bool backFacing(const Meshlet &meshlet, const vec3 &camera);

/// The six planes of the view frustum of @p viewProj (Vulkan clip space: 0 <= z <= w), as
/// (normal, distance) with unit normals pointing inwards: a point p is inside if dot(n, p) + d >= 0
/// for all six. Order: left, right, top, bottom, near, far.
std::array<vec4, 6> frustumPlanes(const mat4 &viewProj);

/// Whether a sphere lies entirely outside one of @p planes (from frustumPlanes).
bool sphereOutsideFrustum(const std::array<vec4, 6> &planes, const vec3 &center, float radius);

} // namespace phox

#endif // PHOX_MESHLETS_HPP
