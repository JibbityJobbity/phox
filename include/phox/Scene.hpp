#ifndef PHOX_SCENE_HPP
#define PHOX_SCENE_HPP

#include "phox/Common.hpp"
#include "phox/RenderPipeline.hpp"
#include "phox/VertexLayout.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace phox {

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

struct Material {
    std::string name;
    std::string shaderName;
    VertexLayout vertexLayout;
    std::array<float, 4> albedo{1.0f, 0.2f, 0.4f, 1.0f};
};

class Mesh {
public:
    Mesh() = default;
    explicit Mesh(std::vector<Triangle> triangles)
        : m_triangles(std::move(triangles)) {}

    void addTriangle(Triangle triangle) { m_triangles.push_back(std::move(triangle)); }
    void addTriangles(std::vector<Triangle> triangles) {
        m_triangles.insert(m_triangles.end(), triangles.begin(), triangles.end());
    }

    const std::vector<Triangle>& triangles() const { return m_triangles; }
    std::size_t triangleCount() const { return m_triangles.size(); }

private:
    std::vector<Triangle> m_triangles;
};

class Object {
public:
    Object(std::string name, Mesh mesh, Material material)
        : m_name(std::move(name)), m_mesh(std::move(mesh)), m_material(std::move(material)) {}

    const std::string& name() const { return m_name; }
    Mesh& mesh() { return m_mesh; }
    const Mesh& mesh() const { return m_mesh; }
    Material& material() { return m_material; }
    const Material& material() const { return m_material; }

private:
    std::string m_name;
    Mesh m_mesh;
    Material m_material;
};

class Scene {
public:
    void addObject(std::unique_ptr<Object> object) { m_objects.push_back(std::move(object)); }

    const std::vector<std::unique_ptr<Object>>& objects() const { return m_objects; }
    std::size_t objectCount() const { return m_objects.size(); }

private:
    std::vector<std::unique_ptr<Object>> m_objects;
};

class ObjectFactory {
public:
    static std::unique_ptr<Object> createTriangle(const std::string& name, Material material) {
        Mesh mesh;
        mesh.addTriangle(Triangle{{MeshVertex{0.0f, 0.0f, 0.0f}}, {MeshVertex{1.0f, 0.0f, 0.0f}}, {MeshVertex{0.0f, 1.0f, 0.0f}}});
        return std::make_unique<Object>(name, std::move(mesh), std::move(material));
    }
};

} // namespace phox

#endif // PHOX_SCENE_HPP
