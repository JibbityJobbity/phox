#ifndef PHOX_SCENE_HPP
#define PHOX_SCENE_HPP

#include "phox/Camera.hpp"
#include "phox/Math.hpp"
#include "phox/Texture.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phox {

/// The vertex format of every Phox mesh (VertexLayout::positionNormalTexcoordTangent()).
struct Vertex {
    vec3 position{0.0f};
    vec3 normal{0.0f, 0.0f, 1.0f};
    vec2 uv{0.0f};
    /// xyz: the direction of increasing u, perpendicular to the normal; w: +1 or -1, the handedness of
    /// the bitangent (cross(normal, tangent) * w points along increasing v). Leave it zero and Mesh works
    /// it out from the texture coordinates.
    vec4 tangent{0.0f};
};
static_assert(sizeof(Vertex) == 48, "Vertex must match VertexLayout::positionNormalTexcoordTangent()");

/// An axis-aligned box.
struct Bounds {
    vec3 min{0.0f};
    vec3 max{0.0f};
    vec3 center() const { return (min + max) * 0.5f; }
    vec3 size() const { return max - min; }
};

/// Indexed triangles. Each Mesh has an id that is never reused and a version that changes whenever its
/// geometry does, so renderers can cache GPU copies. Copies get a new id.
///
/// If any vertex has a zero tangent (w == 0), the mesh computes tangents for all of its vertices from
/// their positions and texture coordinates (see generateTangents).
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
    /// A torus about the Y axis with @p rings segments around that axis and @p sides around the tube.
    static Mesh torus(float radius = 1.0f, float tubeRadius = 0.35f, std::uint32_t rings = 48, std::uint32_t sides = 24);
    /// A UV sphere centred on the origin, poles on the Y axis; u runs once around, v from top to bottom.
    static Mesh sphere(float radius = 0.5f, std::uint32_t segments = 48, std::uint32_t rings = 24);

private:
    std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    Bounds m_bounds;
    std::uint64_t m_id;
    std::uint64_t m_version = 1;
};

/// Computes per-vertex tangents for normal mapping from the triangles' texture coordinates (Lengyel's
/// method: per-triangle tangents summed at each vertex, then made perpendicular to the normal). Vertices
/// whose triangles have no usable texture coordinates get an arbitrary tangent perpendicular to their normal.
void generateTangents(std::vector<Vertex> &vertices, const std::vector<std::uint32_t> &indices);

/// How a material's alpha is used (glTF's alphaMode).
enum class AlphaMode : std::uint8_t {
    Opaque, ///< alpha is ignored
    Mask,   ///< fragments with alpha below alphaCutoff are discarded, the rest are opaque
    Blend,  ///< blended over what is behind, after the opaque objects and back to front; no depth writes
};

/// A metallic-roughness material, as in glTF 2.0. Each texture is optional; where present it is
/// multiplied by its factor. Colours are linear (see phox/Color.hpp).
struct Material {
    std::string name;
    vec4 albedo{1.0f, 0.2f, 0.4f, 1.0f}; ///< base colour factor; rgb linear, a: opacity
    float metallic = 0.0f;               ///< 0: dielectric, 1: metal
    float roughness = 0.5f;              ///< perceptual roughness: 0 mirror-like, 1 fully rough
    vec3 emissive{0.0f};                 ///< emitted light, linear; added after lighting
    float normalScale = 1.0f;            ///< scales the normal map's x and y
    float occlusionStrength = 1.0f;      ///< 0: occlusion texture ignored, 1: fully applied
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f; ///< AlphaMode::Mask only
    /// Both faces of each triangle are drawn (and lit). Turn it off for closed meshes seen from outside:
    /// their back faces are then culled, and the mesh shading path skips whole meshlets that face away.
    bool doubleSided = true;

    std::shared_ptr<const Texture> baseColorTexture{};         ///< rgb: colour (sRGB), a: opacity
    std::shared_ptr<const Texture> metallicRoughnessTexture{}; ///< g: roughness, b: metallic
    std::shared_ptr<const Texture> normalTexture{};            ///< tangent-space normal
    std::shared_ptr<const Texture> occlusionTexture{};         ///< r: ambient occlusion
    std::shared_ptr<const Texture> emissiveTexture{};          ///< rgb: emitted colour (sRGB)

    /// Every texture the material uses, base colour first; null entries for those it does not.
    std::array<const Texture *, 5> textures() const {
        return {baseColorTexture.get(), metallicRoughnessTexture.get(), normalTexture.get(), occlusionTexture.get(),
                emissiveTexture.get()};
    }
};

/// Scale, then rotate, then translate.
struct Transform {
    vec3 position{0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f};
    mat4 matrix() const;
    /// The transform whose matrix() is @p m, for affine matrices without shear (shear is dropped). A
    /// mirroring matrix gets a negative x scale.
    static Transform fromMatrix(const mat4 &m);
};

/// A mesh placed in a scene with a material. Each object has an id that is never reused, so renderers
/// can keep per-object state across frames (e.g. which meshlets were visible). Copies get a new id.
class Object {
public:
    Object(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform = {});
    /// A copy is a different object: it gets its own id. A move keeps the id, so the Scene can move its
    /// objects around (growing, or filling the hole of a removed one) without renderers losing track.
    Object(const Object &other);
    Object &operator=(const Object &other);
    Object(Object &&) noexcept = default;
    Object &operator=(Object &&) noexcept = default;

    std::uint64_t id() const { return m_id; }
    const std::string &name() const { return m_name; }
    const std::shared_ptr<const Mesh> &mesh() const { return m_mesh; }
    void setMesh(std::shared_ptr<const Mesh> mesh) { m_mesh = std::move(mesh); }
    Material &material() { return m_material; }
    const Material &material() const { return m_material; }
    Transform &transform() { return m_transform; }
    const Transform &transform() const { return m_transform; }

private:
    std::uint64_t m_id;
    std::string m_name;
    std::shared_ptr<const Mesh> m_mesh;
    Material m_material;
    Transform m_transform;
};

struct DirectionalLight {
    vec3 direction{-0.4f, -1.0f, -0.6f}; ///< the way the light travels (need not be normalised)
    vec3 color{1.0f};
    /// Light that reaches every surface regardless of direction, as a fraction of the light colour (a
    /// stand-in for environment lighting).
    float ambient = 0.25f;
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

/// Applies @p mapping to a linear colour and encodes the result for the display with the sRGB transfer
/// function, giving values in [0, 1]. The tonemap shader does the same on the GPU.
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

    vec4 clearColor{0.0100228f, 0.0331048f, 0.0732389f, 1.0f}; ///< linear; sRGB (0.1, 0.2, 0.3)
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
