#ifndef PHOX_SCENE_HPP
#define PHOX_SCENE_HPP

#include "phox/Camera.hpp"
#include "phox/Math.hpp"

#include <cstdint>
#include <memory>
#include <span>
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

enum class ToneMapOperator : std::uint8_t {
    Clamp,    ///< exposure, then clamp to [0, 1]; colours already in range pass through unchanged
    Reinhard, ///< c / (1 + c)
    Aces,     ///< Narkowicz's fit of the ACES filmic curve
};

/// How the renderer maps scene colours (which may exceed 1) to the display range.
struct ToneMapping {
    ToneMapOperator op = ToneMapOperator::Clamp;
    float exposure = 1.0f; ///< colours are multiplied by this first
};

/// Applies @p mapping to a linear colour. The tonemap shader does the same on the GPU.
vec3 applyToneMapping(const vec3 &color, const ToneMapping &mapping);

/// Names an object in a Scene. It stays valid until that object is removed; after that the scene rejects
/// it, even once the slot holds a new object (the generation differs). A copy of a scene keeps its ids.
struct ObjectId {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;
    /// Whether the id was ever handed out (a default-constructed id is not). Use Scene::contains() to
    /// ask whether the object still exists.
    bool valid() const { return index != UINT32_MAX; }
    friend bool operator==(const ObjectId &, const ObjectId &) = default;
};

/// What to draw and how to look at it. Plain data: renderers read it and never keep references past a
/// render() call.
///
/// Objects are stored contiguously. Keep ObjectIds, not Object pointers or references: those are only
/// valid until the next addObject() or removeObject().
class Scene {
public:
    ObjectId addObject(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform = {});
    /// Removes the object; the order of the remaining objects may change. Returns false if @p id is stale.
    bool removeObject(ObjectId id);
    bool contains(ObjectId id) const;

    /// The object, or nullptr if @p id is stale.
    Object *get(ObjectId id);
    const Object *get(ObjectId id) const;
    /// The object. @throws std::out_of_range if @p id is stale
    Object &at(ObjectId id);
    const Object &at(ObjectId id) const;

    /// The first object called @p name, or an invalid id. A linear search, for tools and tests; keep the
    /// id that addObject() returns instead.
    ObjectId find(std::string_view name) const;

    /// Every object, in no particular order. objectIds()[i] is the id of objects()[i].
    std::span<Object> objects() { return m_objects; }
    std::span<const Object> objects() const { return m_objects; }
    std::span<const ObjectId> objectIds() const { return m_ids; }
    std::size_t objectCount() const { return m_objects.size(); }

    Camera &camera() { return m_camera; }
    const Camera &camera() const { return m_camera; }
    DirectionalLight &light() { return m_light; }
    const DirectionalLight &light() const { return m_light; }

    vec4 clearColor{0.1f, 0.2f, 0.3f, 1.0f};
    ToneMapping toneMapping;

private:
    struct Slot {
        std::uint32_t dense = 0;      ///< the object's position in m_objects, while the slot is in use
        std::uint32_t generation = 0; ///< bumped on removal, so ids into the old object go stale
    };
    const Object *lookup(ObjectId id) const;

    std::vector<Object> m_objects;
    std::vector<ObjectId> m_ids; ///< parallel to m_objects
    std::vector<Slot> m_slots;   ///< indexed by ObjectId::index
    std::vector<std::uint32_t> m_freeSlots;
    Camera m_camera;
    DirectionalLight m_light;
};

} // namespace phox

#endif // PHOX_SCENE_HPP
