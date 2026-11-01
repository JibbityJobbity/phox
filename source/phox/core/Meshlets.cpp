#include "phox/Meshlets.hpp"

#include "phox/Scene.hpp"

#include <meshoptimizer.h>

#include <cstddef>

namespace phox {

std::size_t MeshletMesh::triangleCount() const {
    std::size_t count = 0;
    for (const Meshlet &meshlet : meshlets) count += meshlet.triangleCount;
    return count;
}

std::vector<std::uint32_t> optimizeIndexOrder(const Mesh &mesh) {
    std::vector<std::uint32_t> indices(mesh.indices().size());
    meshopt_optimizeVertexCache(indices.data(), mesh.indices().data(), mesh.indices().size(), mesh.vertices().size());
    return indices;
}

MeshletMesh buildMeshlets(const Mesh &mesh, const std::vector<std::uint32_t> &indices) {
    const auto &vertices = mesh.vertices();
    const float *positions = &vertices.data()->position.x;
    const std::size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), kMeshletMaxVertices, kMeshletMaxTriangles);
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<unsigned int> meshletVertices(maxMeshlets * kMeshletMaxVertices);
    std::vector<unsigned char> meshletTriangles(maxMeshlets * kMeshletMaxTriangles * 3);
    // A cone weight of 0.25 trades a little vertex reuse for tighter normal cones, so more meshlets can
    // be culled as back-facing.
    meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
                                          indices.data(), indices.size(), positions, vertices.size(), sizeof(Vertex),
                                          kMeshletMaxVertices, kMeshletMaxTriangles, 0.25f));

    MeshletMesh result;
    result.meshlets.reserve(meshlets.size());
    for (const meshopt_Meshlet &m : meshlets) {
        unsigned int *localVertices = &meshletVertices[m.vertex_offset];
        unsigned char *localTriangles = &meshletTriangles[m.triangle_offset];
        meshopt_optimizeMeshlet(localVertices, localTriangles, m.triangle_count, m.vertex_count);
        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(localVertices, localTriangles, m.triangle_count,
                                                                   positions, vertices.size(), sizeof(Vertex));
        Meshlet meshlet;
        meshlet.sphere = {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius};
        // meshoptimizer reports "no usable cone" as a cutoff of exactly 1, which rounding could still
        // satisfy; anything above 1 never is.
        const float cutoff = bounds.cone_cutoff >= 1.0f ? 2.0f : bounds.cone_cutoff;
        meshlet.cone = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], cutoff};
        meshlet.coneApex = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0.0f};
        meshlet.vertexOffset = static_cast<std::uint32_t>(result.data.size());
        meshlet.vertexCount = m.vertex_count;
        result.data.insert(result.data.end(), localVertices, localVertices + m.vertex_count);
        meshlet.triangleOffset = static_cast<std::uint32_t>(result.data.size());
        meshlet.triangleCount = m.triangle_count;
        for (unsigned int t = 0; t < m.triangle_count; ++t) {
            const unsigned char *triangle = &localTriangles[t * 3];
            result.data.push_back(std::uint32_t(triangle[0]) | std::uint32_t(triangle[1]) << 8 |
                                  std::uint32_t(triangle[2]) << 16);
        }
        result.meshlets.push_back(meshlet);
    }
    return result;
}

bool backFacing(const Meshlet &meshlet, const vec3 &camera) {
    const vec3 toApex = vec3(meshlet.coneApex) - camera;
    const float distance = glm::length(toApex);
    if (distance <= 0.0f) {
        return false;
    }
    return glm::dot(toApex / distance, vec3(meshlet.cone)) >= meshlet.cone.w;
}

std::array<vec4, 6> frustumPlanes(const mat4 &viewProj) {
    // Rows of the matrix: clip = (r0.p, r1.p, r2.p, r3.p) for p = (x, y, z, 1).
    const auto row = [&](int i) { return vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]); };
    std::array<vec4, 6> planes{
        row(3) + row(0), // -w <= x
        row(3) - row(0), //  x <= w
        row(3) + row(1), // -w <= y
        row(3) - row(1), //  y <= w
        row(2),          //  0 <= z
        row(3) - row(2), //  z <= w
    };
    for (vec4 &plane : planes) {
        plane /= glm::length(vec3(plane));
    }
    return planes;
}

bool sphereOutsideFrustum(const std::array<vec4, 6> &planes, const vec3 &center, float radius) {
    for (const vec4 &plane : planes) {
        if (glm::dot(vec3(plane), center) + plane.w < -radius) {
            return true;
        }
    }
    return false;
}

} // namespace phox
