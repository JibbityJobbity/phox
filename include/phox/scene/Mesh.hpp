#ifndef PHOX_SCENE_MESH_HPP
#define PHOX_SCENE_MESH_HPP

#include <cstddef>
#include <utility>
#include <vector>

namespace phox::scene {

struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Triangle {
    MeshVertex a;
    MeshVertex b;
    MeshVertex c;
};

class Mesh {
  public:
    Mesh() = default;
    explicit Mesh(std::vector<Triangle> triangles) : m_triangles(std::move(triangles)) {}

    void addTriangle(Triangle triangle) { m_triangles.push_back(std::move(triangle)); }
    void addTriangles(std::vector<Triangle> triangles) {
        m_triangles.insert(m_triangles.end(), triangles.begin(), triangles.end());
    }

    const std::vector<Triangle> &triangles() const { return m_triangles; }
    std::size_t triangleCount() const { return m_triangles.size(); }

  private:
    std::vector<Triangle> m_triangles;
};

} // namespace phox::scene

#endif // PHOX_SCENE_MESH_HPP
