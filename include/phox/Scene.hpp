#ifndef PHOX_SCENE_HPP
#define PHOX_SCENE_HPP

#include "phox/Camera.hpp"
#include "phox/Math.hpp"
#include "phox/Texture.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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

/// A Scene's record of which objects changed, by ObjectId::index: Scene::changes() reads it. Objects
/// append themselves through their non-const accessors, at most once until a reader has read past them.
class SceneChangeLog {
public:
    SceneChangeLog();
    /// Logs object slot @p index, unless it is logged and unread. @p loggedAt is the object's own record
    /// of where (one past its last entry; 0: never).
    void record(std::uint32_t index, std::uint64_t &loggedAt);
    std::uint64_t id() const { return m_id; }

private:
    friend class Scene;
    std::uint64_t m_id;                  ///< never reused, so a reader can tell logs apart
    std::vector<std::uint32_t> m_entries;
    std::uint64_t m_first = 0;           ///< the position of m_entries[0]
    std::uint64_t m_read = 0;            ///< the furthest position a reader has read to
    std::size_t m_limit = 1024;          ///< past this many entries the log starts over (Scene sets it)
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
///
/// Renderers also keep copies of what they drew (the Vulkan renderer keeps every object on the GPU), and
/// find what changed through two revisions: the non-const transform() and setMesh() bump
/// transformRevision(), the non-const material() bumps materialRevision(). An object in a Scene also
/// logs itself in the scene's change log (Scene::changes). So change an object through those accessors
/// before each render(), and do not keep the references they return across frames: a write through an
/// old reference is not seen. Mesh geometry is versioned by the Mesh itself.
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
    void setMesh(std::shared_ptr<const Mesh> mesh) {
        m_mesh = std::move(mesh);
        ++m_transformRevision;
        logChange();
    }
    /// For changing the material: bumps materialRevision().
    Material &material() {
        ++m_materialRevision;
        logChange();
        return m_material;
    }
    const Material &material() const { return m_material; }
    /// For changing the transform: bumps transformRevision().
    Transform &transform() {
        ++m_transformRevision;
        logChange();
        return m_transform;
    }
    const Transform &transform() const { return m_transform; }
    /// Changes whenever the transform or mesh may have.
    std::uint64_t transformRevision() const { return m_transformRevision; }
    /// Changes whenever the material may have.
    std::uint64_t materialRevision() const { return m_materialRevision; }

private:
    friend class Scene;
    void logChange() {
        if (m_log != nullptr) {
            m_log->record(m_index, m_loggedAt);
        }
    }

    std::uint64_t m_id;
    std::uint64_t m_transformRevision = 1;
    std::uint64_t m_materialRevision = 1;
    // In a Scene: its change log, the object's ObjectId::index, and where it last logged itself.
    SceneChangeLog *m_log = nullptr;
    std::uint32_t m_index = 0;
    std::uint64_t m_loggedAt = 0;
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
    /// Where a reader of changes() has got to.
    struct ChangeCursor {
        std::uint64_t log = 0; ///< SceneChangeLog::id of the scene read; 0: none yet
        std::uint64_t position = 0;
    };

    Scene();
    /// A copy keeps the ObjectIds; its objects are copies, so they get new Object::ids.
    Scene(const Scene &other);
    Scene &operator=(const Scene &other);
    Scene(Scene &&) noexcept = default;
    Scene &operator=(Scene &&) noexcept = default;
    ~Scene();

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

    /// The object whose ObjectId::index is @p index, or nullptr if there is none.
    const Object *objectAt(std::uint32_t index) const;

    /// The ObjectId::index of every object added, removed or changed through its non-const accessors
    /// (see Object) since @p cursor last read this scene, and moves @p cursor to now. Indices may repeat,
    /// and an index may name an object since removed (objectAt() is null) or replaced. Empty (nullopt)
    /// when the scene cannot tell: @p cursor has not read this scene, or the entries it needs were dropped
    /// (the log starts over once it outgrows the scene); then look at every object. For renderers that
    /// keep copies of the objects; not thread-safe.
    std::optional<std::span<const std::uint32_t>> changes(ChangeCursor &cursor) const;

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
    SceneChangeLog &log();

    std::vector<Object> m_objects;
    std::vector<ObjectId> m_ids; ///< parallel to m_objects
    std::vector<Slot> m_slots;   ///< indexed by ObjectId::index
    std::vector<std::uint32_t> m_freeSlots;
    std::unique_ptr<SceneChangeLog> m_log; ///< on the heap, so objects' pointers survive moving the scene
    Camera m_camera;
    DirectionalLight m_light;
};

} // namespace phox

#endif // PHOX_SCENE_HPP
