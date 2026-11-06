#include "phox/Meshlets.hpp"

#include "phox/Scene.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>

namespace phox {

std::size_t MeshletMesh::triangleCount() const {
    std::size_t count = 0;
    for (const Meshlet &meshlet : meshlets) count += meshlet.triangleCount;
    return count;
}

std::size_t MeshletMesh::triangleCount(std::uint32_t level) const {
    std::size_t count = 0;
    for (const Meshlet &meshlet : meshlets) count += meshlet.level == level ? meshlet.triangleCount : 0;
    return count;
}

std::vector<std::uint32_t> optimizeIndexOrder(const Mesh &mesh) {
    std::vector<std::uint32_t> indices(mesh.indices().size());
    meshopt_optimizeVertexCache(indices.data(), mesh.indices().data(), mesh.indices().size(), mesh.vertices().size());
    return indices;
}

namespace {

// A meshlet while the hierarchy is built: its triangles as indices into the mesh's vertices, and the
// LOD fields that end up in its Meshlet.
struct Cluster {
    std::vector<unsigned int> indices;
    vec4 lodSphere{0.0f};
    float lodError = 0.0f;
    vec4 parentSphere{0.0f};
    float parentError = kLodNoParent;
    std::uint32_t level = 0;
};

// Splits the triangles in @p indices into clusters of at most kMeshletMaxVertices vertices and
// kMeshletMaxTriangles triangles, keeping close triangles together. The triangles' vertices are first
// copied into a compact array, so the work depends on the triangles given, not on the whole mesh.
void clusterize(const Mesh &mesh, const std::vector<unsigned int> &indices, std::uint32_t level,
                std::vector<Cluster> &out) {
    std::vector<unsigned int> global; // local vertex -> mesh vertex
    std::vector<unsigned int> local(indices.size());
    std::unordered_map<unsigned int, unsigned int> toLocal;
    toLocal.reserve(indices.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto [it, inserted] = toLocal.try_emplace(indices[i], static_cast<unsigned int>(global.size()));
        if (inserted) {
            global.push_back(indices[i]);
        }
        local[i] = it->second;
    }
    std::vector<vec3> positions(global.size());
    for (std::size_t i = 0; i < global.size(); ++i) positions[i] = mesh.vertices()[global[i]].position;

    const std::size_t maxMeshlets = meshopt_buildMeshletsBound(local.size(), kMeshletMaxVertices, kMeshletMaxTriangles);
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<unsigned int> meshletVertices(maxMeshlets * kMeshletMaxVertices);
    std::vector<unsigned char> meshletTriangles(maxMeshlets * kMeshletMaxTriangles * 3);
    // A cone weight of 0.25 trades a little vertex reuse for tighter normal cones, so more meshlets can
    // be culled as back-facing.
    meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), local.data(),
                                          local.size(), &positions.data()->x, positions.size(), sizeof(vec3),
                                          kMeshletMaxVertices, kMeshletMaxTriangles, 0.25f));
    for (const meshopt_Meshlet &m : meshlets) {
        Cluster cluster;
        cluster.level = level;
        cluster.indices.reserve(m.triangle_count * 3);
        for (unsigned int i = 0; i < m.triangle_count * 3; ++i) {
            cluster.indices.push_back(global[meshletVertices[m.vertex_offset + meshletTriangles[m.triangle_offset + i]]]);
        }
        out.push_back(std::move(cluster));
    }
}

// Appends @p cluster to @p result as a Meshlet: local vertex and triangle lists ordered for the GPU,
// culling bounds from its geometry, and its LOD fields.
void appendMeshlet(const Mesh &mesh, Cluster &cluster, MeshletMesh &result) {
    const auto &vertices = mesh.vertices();
    const float *positions = &vertices.data()->position.x;
    const std::size_t triangleCount = cluster.indices.size() / 3;
    std::vector<unsigned int> localVertices(cluster.indices.size());
    std::vector<unsigned char> localTriangles(cluster.indices.size());
    const std::size_t vertexCount = meshopt_extractMeshletIndices(localVertices.data(), localTriangles.data(),
                                                                  cluster.indices.data(), cluster.indices.size());
    meshopt_optimizeMeshlet(localVertices.data(), localTriangles.data(), triangleCount, vertexCount);
    const meshopt_Bounds bounds = meshopt_computeMeshletBounds(localVertices.data(), localTriangles.data(),
                                                               triangleCount, positions, vertices.size(), sizeof(Vertex));
    Meshlet meshlet;
    meshlet.sphere = {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius};
    // meshoptimizer reports "no usable cone" as a cutoff of exactly 1, which rounding could still
    // satisfy; anything above 1 never is.
    const float cutoff = bounds.cone_cutoff >= 1.0f ? 2.0f : bounds.cone_cutoff;
    meshlet.cone = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], cutoff};
    meshlet.coneApex = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0.0f};
    meshlet.lodSphere = cluster.lodSphere;
    meshlet.lodError = cluster.lodError;
    meshlet.parentSphere = cluster.parentSphere;
    meshlet.parentError = cluster.parentError;
    meshlet.level = cluster.level;
    meshlet.vertexOffset = static_cast<std::uint32_t>(result.data.size());
    meshlet.vertexCount = static_cast<std::uint32_t>(vertexCount);
    result.data.insert(result.data.end(), localVertices.begin(), localVertices.begin() + vertexCount);
    meshlet.triangleOffset = static_cast<std::uint32_t>(result.data.size());
    meshlet.triangleCount = static_cast<std::uint32_t>(triangleCount);
    for (std::size_t t = 0; t < triangleCount; ++t) {
        const unsigned char *triangle = &localTriangles[t * 3];
        result.data.push_back(std::uint32_t(triangle[0]) | std::uint32_t(triangle[1]) << 8 | std::uint32_t(triangle[2]) << 16);
    }
    result.meshlets.push_back(meshlet);
    result.levels = std::max(result.levels, cluster.level + 1);
}

vec4 clusterSphere(const Mesh &mesh, const std::vector<unsigned int> &indices) {
    const meshopt_Bounds bounds = meshopt_computeClusterBounds(indices.data(), indices.size(),
                                                               &mesh.vertices().data()->position.x,
                                                               mesh.vertices().size(), sizeof(Vertex));
    return {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius};
}

// Groups of clusters that share many vertices, about kGroupSize each, as lists of indices into
// @p clusters. Connectivity is by position (@p remap), so seams do not separate neighbours.
std::vector<std::vector<std::size_t>> partition(const Mesh &mesh, const std::vector<Cluster> &all,
                                                const std::vector<std::size_t> &clusters,
                                                const std::vector<unsigned int> &remap, std::size_t groupSize) {
    if (clusters.size() <= groupSize) {
        return {clusters};
    }
    std::vector<unsigned int> indices;
    std::vector<unsigned int> counts;
    counts.reserve(clusters.size());
    for (std::size_t c : clusters) {
        counts.push_back(static_cast<unsigned int>(all[c].indices.size()));
        for (unsigned int v : all[c].indices) indices.push_back(remap[v]);
    }
    std::vector<unsigned int> part(clusters.size());
    const std::size_t partCount =
        meshopt_partitionClusters(part.data(), indices.data(), indices.size(), counts.data(), counts.size(),
                                  &mesh.vertices().data()->position.x, mesh.vertices().size(), sizeof(Vertex), groupSize);
    std::vector<std::vector<std::size_t>> groups(partCount);
    for (std::size_t i = 0; i < clusters.size(); ++i) groups[part[i]].push_back(clusters[i]);
    return groups;
}

} // namespace

MeshletMesh buildMeshlets(const Mesh &mesh, const std::vector<std::uint32_t> &indices) {
    std::vector<Cluster> clusters;
    clusterize(mesh, indices, 0, clusters);
    MeshletMesh result;
    result.meshlets.reserve(clusters.size());
    for (Cluster &cluster : clusters) {
        cluster.lodSphere = clusterSphere(mesh, cluster.indices);
        appendMeshlet(mesh, cluster, result);
    }
    return result;
}

MeshletMesh buildMeshletLod(const Mesh &mesh, const std::vector<std::uint32_t> &indices) {
    // Clusters per group, and how far a group must simplify to count as progress (as in meshoptimizer's
    // clusterlod example): groups that keep more than 85% of their triangles are left as they are.
    constexpr std::size_t kGroupSize = 16;
    constexpr float kSimplifyRatio = 0.5f;
    constexpr float kStuckRatio = 0.85f;

    const auto &vertices = mesh.vertices();
    const float *positions = &vertices.data()->position.x;
    // Vertices with the same position, for connectivity and locking: seams must not open up.
    std::vector<unsigned int> remap(vertices.size());
    meshopt_generatePositionRemap(remap.data(), positions, vertices.size(), sizeof(Vertex));

    std::vector<Cluster> all; // every cluster of every level, in the order they are made
    clusterize(mesh, indices, 0, all);
    for (Cluster &cluster : all) cluster.lodSphere = clusterSphere(mesh, cluster.indices);
    std::vector<std::size_t> current(all.size());
    for (std::size_t i = 0; i < current.size(); ++i) current[i] = i;

    std::vector<int> owner(vertices.size());
    std::vector<unsigned char> sharedVertex(vertices.size());
    std::vector<unsigned char> locks(vertices.size());
    std::vector<unsigned int> merged;
    std::vector<unsigned int> simplified;
    for (std::uint32_t level = 0; current.size() > 1; ++level) {
        const std::vector<std::vector<std::size_t>> groups = partition(mesh, all, current, remap, kGroupSize);

        // Lock every position used by more than one group: both sides of a group border then keep the
        // same edge, so whatever level each side is drawn at, they meet without a crack.
        std::fill(owner.begin(), owner.end(), -1);
        std::fill(sharedVertex.begin(), sharedVertex.end(), 0);
        for (std::size_t g = 0; g < groups.size(); ++g) {
            for (std::size_t c : groups[g]) {
                for (unsigned int v : all[c].indices) {
                    int &o = owner[remap[v]];
                    if (o < 0) {
                        o = static_cast<int>(g);
                    } else if (o != static_cast<int>(g)) {
                        sharedVertex[remap[v]] = 1;
                    }
                }
            }
        }
        for (std::size_t v = 0; v < vertices.size(); ++v) {
            locks[v] = sharedVertex[remap[v]] != 0 ? meshopt_SimplifyVertex_Lock : 0;
        }

        std::vector<std::size_t> next;
        for (const std::vector<std::size_t> &group : groups) {
            merged.clear();
            std::vector<vec4> spheres;
            float groupError = 0.0f;
            for (std::size_t c : group) {
                merged.insert(merged.end(), all[c].indices.begin(), all[c].indices.end());
                spheres.push_back(all[c].lodSphere);
                groupError = std::max(groupError, all[c].lodError);
            }
            // The group's bounds enclose its clusters' (not its own geometry, which could be smaller):
            // spheres and errors then never shrink from one level to the next.
            const meshopt_Bounds merge =
                meshopt_computeSphereBounds(&spheres.data()->x, spheres.size(), sizeof(vec4), &spheres.data()->w, sizeof(vec4));
            const vec4 groupSphere{merge.center[0], merge.center[1], merge.center[2], merge.radius};

            const std::size_t target = static_cast<std::size_t>(static_cast<float>(merged.size() / 3) * kSimplifyRatio) * 3;
            simplified.resize(merged.size());
            float error = 0.0f;
            // Positions only, so the error is geometric (what lodSelected measures in pixels). Attribute seams
            // are kept anyway: vertices that share a position but not attributes only collapse together.
            // Locked: the vertices shared with other groups (by position), and the group's open border,
            // which also covers borders with clusters that stopped simplifying at an earlier level.
            simplified.resize(meshopt_simplifyWithAttributes(
                simplified.data(), merged.data(), merged.size(), positions, vertices.size(), sizeof(Vertex), nullptr, 0,
                nullptr, 0, locks.data(), target, 3.0e38f,
                meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyLockBorder, &error));
            if (simplified.empty() || static_cast<float>(simplified.size()) > static_cast<float>(merged.size()) * kStuckRatio) {
                continue; // stuck: these clusters stay the coarsest version of their part of the mesh
            }
            const float parentError = std::max(groupError, error);
            for (std::size_t c : group) {
                all[c].parentSphere = groupSphere;
                all[c].parentError = parentError;
            }
            const std::size_t first = all.size();
            clusterize(mesh, simplified, level + 1, all);
            for (std::size_t c = first; c < all.size(); ++c) {
                all[c].lodSphere = groupSphere;
                all[c].lodError = parentError;
                next.push_back(c);
            }
        }
        current = std::move(next);
    }

    MeshletMesh result;
    result.meshlets.reserve(all.size());
    for (Cluster &cluster : all) appendMeshlet(mesh, cluster, result);
    return result;
}

float lodProjectedError(const vec4 &sphere, float error, const vec3 &camera, float pixelsPerUnit) {
    if (error >= kLodNoParent) {
        return kLodNoParent;
    }
    // The distance to the sphere, not its centre: a camera inside it sees the error at its largest.
    const float distance = std::max(glm::length(vec3(sphere) - camera) - sphere.w, 1e-6f);
    return error / distance * pixelsPerUnit;
}

bool lodSelected(const Meshlet &meshlet, const vec3 &camera, float pixelsPerUnit, float threshold) {
    return lodProjectedError(meshlet.lodSphere, meshlet.lodError, camera, pixelsPerUnit) <= threshold &&
           lodProjectedError(meshlet.parentSphere, meshlet.parentError, camera, pixelsPerUnit) > threshold;
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

namespace {

// The slopes x/d of the two lines from the eye that touch a circle of radius r centred at (x, d) in one
// plane through the view axis (d: distance in front of the camera, greater than r).
vec2 tangentSlopes(float x, float d, float r) {
    const float l = std::sqrt(x * x + d * d - r * r);
    return {(x * l - d * r) / (d * l + x * r), (x * l + d * r) / (d * l - x * r)};
}

std::uint32_t floorPowerOfTwo(std::uint32_t value) {
    std::uint32_t result = 1;
    while (result * 2 <= value) result *= 2;
    return result;
}

} // namespace

bool projectSphere(const vec3 &center, float radius, const mat4 &proj, vec4 &bounds) {
    const float d = -center.z;
    const float nearZ = proj[3][2] / proj[2][2];
    if (d - radius < nearZ) {
        return false;
    }
    // Clip x = proj[0][0] * view x and clip y = proj[1][1] * view y (negative: Vulkan's y points down),
    // both over w = d.
    const vec2 x = tangentSlopes(center.x, d, radius) * proj[0][0];
    const vec2 y = tangentSlopes(center.y, d, radius) * proj[1][1];
    bounds = vec4(std::min(x.x, x.y), std::min(y.x, y.y), std::max(x.x, x.y), std::max(y.x, y.y)) * 0.5f + 0.5f;
    return true;
}

Extent2D depthPyramidExtent(Extent2D extent) {
    return {floorPowerOfTwo(std::max(extent.width, 1u)), floorPowerOfTwo(std::max(extent.height, 1u))};
}

std::uint32_t depthPyramidLevels(Extent2D extent) {
    std::uint32_t levels = 1;
    for (std::uint32_t size = std::max(extent.width, extent.height); size > 1; size /= 2) ++levels;
    return levels;
}

} // namespace phox
