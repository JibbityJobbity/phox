#ifndef PHOX_SCENE_HPP
#define PHOX_SCENE_HPP

#include "phox/Camera.hpp"
#include "phox/Math.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace phox {

/// The vertex format of every Phox mesh (VertexLayout::positionNormalTexcoord()).
struct Vertex {
    vec3 position{0.0f};
    vec3 normal{0.0f, 0.0f, 1.0f};
    vec2 uv{0.0f};
};
static_assert(sizeof(Vertex) == 32, "Vertex must match VertexLayout::positionNormalTexcoord()");

/// An axis-aligned box.
struct Bounds {
    vec3 min{0.0f};
    vec3 max{0.0f};
    vec3 center() const { return (min + max) * 0.5f; }
    vec3 size() const { return max - min; }
};

/// Indexed triangles. Each Mesh has an id that is never reused and a version that changes whenever its
/// geometry does, so renderers can cache GPU copies. Copies get a new id.
class Mesh {
public:
    Mesh();
    /// @throws std::invalid_argument if the index count is not a multiple of 3 or an index is out of range
    Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);
    Mesh(const Mesh &other);
    Mesh &operator=(const Mesh &other);
    /// The moved-from mesh is left empty with a fresh id.
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    /// Replaces the geometry (same checks as the constructor) and bumps the version.
    void setGeometry(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);

    const std::vector<Vertex> &vertices() const { return m_vertices; }
    const std::vector<std::uint32_t> &indices() const { return m_indices; }
    std::size_t triangleCount() const { return m_indices.size() / 3; }
    bool empty() const { return m_indices.empty(); }
    const Bounds &bounds() const { return m_bounds; }
    std::uint64_t id() const { return m_id; }
    std::uint64_t version() const { return m_version; }

    /// An equilateral triangle centred on the origin, facing +Z, with circumradius @p radius.
    static Mesh triangle(float radius = 1.0f);
    /// A cube centred on the origin with flat-shaded faces (24 vertices).
    static Mesh cube(float size = 1.0f);
    /// A square in the XZ plane facing +Y.
    static Mesh plane(float size = 1.0f);

private:
    std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    Bounds m_bounds;
    std::uint64_t m_id;
    std::uint64_t m_version = 1;
};

struct Material {
    std::string name;
    vec4 albedo{1.0f, 0.2f, 0.4f, 1.0f};
};

/// Scale, then rotate, then translate.
struct Transform {
    vec3 position{0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f};
    mat4 matrix() const;
};

class Object {
public:
    Object(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform = {})
        : m_name(std::move(name)), m_mesh(std::move(mesh)), m_material(std::move(material)), m_transform(transform) {}

    const std::string &name() const { return m_name; }
    const std::shared_ptr<const Mesh> &mesh() const { return m_mesh; }
    void setMesh(std::shared_ptr<const Mesh> mesh) { m_mesh = std::move(mesh); }
    Material &material() { return m_material; }
    const Material &material() const { return m_material; }
    Transform &transform() { return m_transform; }
    const Transform &transform() const { return m_transform; }

private:
    std::string m_name;
    std::shared_ptr<const Mesh> m_mesh;
    Material m_material;
    Transform m_transform;
};

struct DirectionalLight {
    vec3 direction{-0.4f, -1.0f, -0.6f}; ///< the way the light travels (need not be normalised)
    vec3 color{1.0f};
    float ambient = 0.25f; ///< fraction of the albedo that is lit regardless of direction
};

/// What to draw and how to look at it. Plain data: renderers read it and never keep references past a
/// render() call.
class Scene {
public:
    Object &addObject(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform = {});
    /// The first object called @p name, or nullptr.
    Object *find(std::string_view name);

    const std::vector<std::unique_ptr<Object>> &objects() const { return m_objects; }
    std::size_t objectCount() const { return m_objects.size(); }

    Camera &camera() { return m_camera; }
    const Camera &camera() const { return m_camera; }
    DirectionalLight &light() { return m_light; }
    const DirectionalLight &light() const { return m_light; }

    vec4 clearColor{0.1f, 0.2f, 0.3f, 1.0f};

private:
    std::vector<std::unique_ptr<Object>> m_objects;
    Camera m_camera;
    DirectionalLight m_light;
};

} // namespace phox

#endif // PHOX_SCENE_HPP
