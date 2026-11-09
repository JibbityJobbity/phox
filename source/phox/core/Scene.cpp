#include "phox/Scene.hpp"

#include "phox/Color.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace phox {

namespace {

std::uint64_t nextMeshId() {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1);
}

std::uint64_t nextObjectId() {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1);
}

Bounds computeBounds(const std::vector<Vertex> &vertices) {
    if (vertices.empty()) {
        return {};
    }
    Bounds bounds{vertices.front().position, vertices.front().position};
    for (const Vertex &v : vertices) {
        bounds.min = glm::min(bounds.min, v.position);
        bounds.max = glm::max(bounds.max, v.position);
    }
    return bounds;
}

void validate(const std::vector<Vertex> &vertices, const std::vector<std::uint32_t> &indices) {
    if (indices.size() % 3 != 0) {
        throw std::invalid_argument("mesh index count " + std::to_string(indices.size()) + " is not a multiple of 3");
    }
    for (std::uint32_t index : indices) {
        if (index >= vertices.size()) {
            throw std::invalid_argument("mesh index " + std::to_string(index) + " is out of range (" +
                                        std::to_string(vertices.size()) + " vertices)");
        }
    }
}

// A unit vector perpendicular to @p n.
vec3 anyPerpendicular(const vec3 &n) {
    const vec3 axis = std::abs(n.x) < 0.9f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, n));
}

} // namespace

void generateTangents(std::vector<Vertex> &vertices, const std::vector<std::uint32_t> &indices) {
    std::vector<vec3> tangents(vertices.size(), vec3(0.0f));
    std::vector<vec3> bitangents(vertices.size(), vec3(0.0f));
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const Vertex &a = vertices[indices[t]];
        const Vertex &b = vertices[indices[t + 1]];
        const Vertex &c = vertices[indices[t + 2]];
        const vec3 e1 = b.position - a.position;
        const vec3 e2 = c.position - a.position;
        const vec2 d1 = b.uv - a.uv;
        const vec2 d2 = c.uv - a.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) {
            continue; // no usable texture mapping on this triangle
        }
        // Unnormalised, so larger triangles weigh more (area weighting).
        const vec3 tangent = (e1 * d2.y - e2 * d1.y) / det;
        const vec3 bitangent = (e2 * d1.x - e1 * d2.x) / det;
        for (std::size_t k = 0; k < 3; ++k) {
            tangents[indices[t + k]] += tangent;
            bitangents[indices[t + k]] += bitangent;
        }
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const vec3 n = glm::normalize(vertices[i].normal);
        // Gram-Schmidt: remove the part along the normal.
        vec3 t = tangents[i] - n * glm::dot(n, tangents[i]);
        const float length = glm::length(t);
        t = length > 1e-12f && std::isfinite(length) ? t / length : anyPerpendicular(n);
        const float w = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = vec4(t, w);
    }
}

Mesh::Mesh() : m_id(nextMeshId()) {}

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices) : m_id(nextMeshId()) {
    setGeometry(std::move(vertices), std::move(indices));
    m_version = 1;
}

Mesh::Mesh(const Mesh &other)
    : m_vertices(other.m_vertices), m_indices(other.m_indices), m_bounds(other.m_bounds), m_id(nextMeshId()) {}

Mesh &Mesh::operator=(const Mesh &other) {
    if (this != &other) {
        m_vertices = other.m_vertices;
        m_indices = other.m_indices;
        m_bounds = other.m_bounds;
        ++m_version;
    }
    return *this;
}

Mesh::Mesh(Mesh &&other) noexcept
    : m_vertices(std::move(other.m_vertices)), m_indices(std::move(other.m_indices)), m_bounds(other.m_bounds),
      m_id(other.m_id), m_version(other.m_version) {
    other.m_vertices.clear();
    other.m_indices.clear();
    other.m_bounds = {};
    other.m_id = nextMeshId();
    other.m_version = 1;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept {
    if (this != &other) {
        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_bounds = other.m_bounds;
        m_id = other.m_id;
        m_version = other.m_version;
        other.m_vertices.clear();
        other.m_indices.clear();
        other.m_bounds = {};
        other.m_id = nextMeshId();
        other.m_version = 1;
    }
    return *this;
}

void Mesh::setGeometry(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices) {
    validate(vertices, indices);
    if (std::any_of(vertices.begin(), vertices.end(), [](const Vertex &v) { return v.tangent.w == 0.0f; })) {
        generateTangents(vertices, indices);
    }
    m_vertices = std::move(vertices);
    m_indices = std::move(indices);
    m_bounds = computeBounds(m_vertices);
    ++m_version;
}

Mesh Mesh::triangle(float radius) {
    const float s = std::sqrt(3.0f) * 0.5f * radius;
    const vec3 normal{0.0f, 0.0f, 1.0f};
    // Counter-clockwise seen from +Z.
    return Mesh({{{0.0f, radius, 0.0f}, normal, {0.5f, 0.0f}},
                 {{-s, -0.5f * radius, 0.0f}, normal, {0.0f, 1.0f}},
                 {{s, -0.5f * radius, 0.0f}, normal, {1.0f, 1.0f}}},
                {0, 1, 2});
}

Mesh Mesh::cube(float size) {
    struct Face {
        vec3 normal, u, v; // u x v == normal, so corners in u/v order wind counter-clockwise from outside
    };
    const std::array<Face, 6> faces{{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};
    const float h = 0.5f * size;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const Face &face : faces) {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        const std::array<vec2, 4> corners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        for (const vec2 &c : corners) {
            vertices.push_back({(face.normal + c.x * face.u + c.y * face.v) * h, face.normal,
                                {0.5f * (c.x + 1.0f), 0.5f * (1.0f - c.y)}});
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(base + i);
    }
    return Mesh(std::move(vertices), std::move(indices));
}

Mesh Mesh::plane(float size) {
    const float h = 0.5f * size;
    const vec3 up{0.0f, 1.0f, 0.0f};
    return Mesh({{{-h, 0.0f, h}, up, {0.0f, 1.0f}},
                 {{h, 0.0f, h}, up, {1.0f, 1.0f}},
                 {{h, 0.0f, -h}, up, {1.0f, 0.0f}},
                 {{-h, 0.0f, -h}, up, {0.0f, 0.0f}}},
                {0, 1, 2, 0, 2, 3});
}

Mesh Mesh::torus(float radius, float tubeRadius, std::uint32_t rings, std::uint32_t sides) {
    rings = std::max(rings, 3u);
    sides = std::max(sides, 3u);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(rings + 1) * (sides + 1));
    // The seam vertices are duplicated so texture coordinates can wrap. Their positions repeat the first
    // ring's exactly (angles from the wrapped index, not 2 pi), so the seam is closed for tools that match
    // vertices by position, such as the cluster LOD builder.
    for (std::uint32_t i = 0; i <= rings; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(rings);
        const float theta = static_cast<float>(i % rings) / static_cast<float>(rings) * glm::two_pi<float>();
        const vec3 around{std::cos(theta), 0.0f, std::sin(theta)};
        for (std::uint32_t j = 0; j <= sides; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(sides);
            const float phi = static_cast<float>(j % sides) / static_cast<float>(sides) * glm::two_pi<float>();
            const vec3 normal = std::cos(phi) * around + vec3(0.0f, std::sin(phi), 0.0f);
            vertices.push_back({radius * around + tubeRadius * normal, normal, {u, v}});
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(rings) * sides * 6);
    const auto at = [sides](std::uint32_t i, std::uint32_t j) { return i * (sides + 1) + j; };
    for (std::uint32_t i = 0; i < rings; ++i) {
        for (std::uint32_t j = 0; j < sides; ++j) {
            // Counter-clockwise seen from outside the tube.
            const std::uint32_t a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            for (std::uint32_t index : {a, d, c, a, c, b}) indices.push_back(index);
        }
    }
    return Mesh(std::move(vertices), std::move(indices));
}

Mesh Mesh::sphere(float radius, std::uint32_t segments, std::uint32_t rings) {
    segments = std::max(segments, 3u);
    rings = std::max(rings, 2u);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(segments + 1) * (rings + 1));
    // Seam and pole vertices are duplicated so each gets its own texture coordinate. Their positions are
    // exactly equal (wrapped angles, exact poles), so the surface is closed for tools that match vertices
    // by position.
    for (std::uint32_t j = 0; j <= rings; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(rings);
        const float phi = v * glm::pi<float>(); // from +Y down to -Y
        const bool pole = j == 0 || j == rings;
        const float sinPhi = pole ? 0.0f : std::sin(phi);
        const float cosPhi = j == 0 ? 1.0f : j == rings ? -1.0f : std::cos(phi);
        for (std::uint32_t i = 0; i <= segments; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(segments);
            const float theta = static_cast<float>(i % segments) / static_cast<float>(segments) * glm::two_pi<float>();
            const vec3 normal{sinPhi * std::cos(theta), cosPhi, -sinPhi * std::sin(theta)};
            // The tangent is d(position)/d(theta), normalised. Increasing v points down, against
            // cross(normal, tangent), hence the handedness of -1.
            const vec3 tangent{-std::sin(theta), 0.0f, -std::cos(theta)};
            vertices.push_back({radius * normal, normal, {u, v}, vec4(tangent, -1.0f)});
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(segments) * rings * 6);
    const auto at = [segments](std::uint32_t i, std::uint32_t j) { return j * (segments + 1) + i; };
    for (std::uint32_t j = 0; j < rings; ++j) {
        for (std::uint32_t i = 0; i < segments; ++i) {
            // Counter-clockwise seen from outside; the degenerate triangles at the poles are skipped.
            const std::uint32_t a = at(i, j), b = at(i, j + 1), c = at(i + 1, j + 1), d = at(i + 1, j);
            if (j != 0) {
                for (std::uint32_t index : {a, b, d}) indices.push_back(index);
            }
            if (j != rings - 1) {
                for (std::uint32_t index : {d, b, c}) indices.push_back(index);
            }
        }
    }
    return Mesh(std::move(vertices), std::move(indices));
}

vec3 applyToneMapping(const vec3 &color, const ToneMapping &mapping) {
    vec3 c = color * mapping.exposure;
    switch (mapping.op) {
    case ToneMapOperator::Clamp:
        break;
    case ToneMapOperator::Reinhard:
        c = c / (vec3(1.0f) + c);
        break;
    case ToneMapOperator::Aces:
        c = (c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f);
        break;
    }
    return linearToSrgb(glm::clamp(c, vec3(0.0f), vec3(1.0f)));
}

mat4 Transform::matrix() const {
    // translate * rotate * scale, composed directly: the rotation's columns scaled, then the translation.
    // The same values as multiplying the three matrices (whose other entries are exact zeros and ones),
    // without the work.
    const mat3 rotate = glm::mat3_cast(rotation);
    return mat4(vec4(rotate[0] * scale.x, 0.0f), vec4(rotate[1] * scale.y, 0.0f), vec4(rotate[2] * scale.z, 0.0f),
                vec4(position, 1.0f));
}

Transform Transform::fromMatrix(const mat4 &m) {
    Transform t;
    t.position = vec3(m[3]);
    mat3 basis{m};
    t.scale = {glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])};
    if (glm::determinant(basis) < 0.0f) {
        t.scale.x = -t.scale.x; // a rotation cannot mirror, so the scale does
    }
    for (int i = 0; i < 3; ++i) {
        if (t.scale[i] != 0.0f) {
            basis[i] /= t.scale[i];
        }
    }
    t.rotation = glm::normalize(glm::quat_cast(basis));
    return t;
}

Object::Object(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform)
    : m_id(nextObjectId()), m_name(std::move(name)), m_mesh(std::move(mesh)), m_material(std::move(material)),
      m_transform(transform) {}

Object::Object(const Object &other)
    : m_id(nextObjectId()), m_name(other.m_name), m_mesh(other.m_mesh), m_material(other.m_material),
      m_transform(other.m_transform) {}

Object &Object::operator=(const Object &other) {
    if (this != &other) {
        m_name = other.m_name;
        m_mesh = other.m_mesh;
        m_material = other.m_material;
        m_transform = other.m_transform; // keeps its own id
    }
    return *this;
}

ObjectId Scene::addObject(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform) {
    std::uint32_t index;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        if (m_slots.size() >= UINT32_MAX) {
            throw std::length_error("Scene: too many objects");
        }
        index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    m_objects.emplace_back(std::move(name), std::move(mesh), std::move(material), transform);
    const ObjectId id{index, m_slots[index].generation};
    m_ids.push_back(id);
    m_slots[index].dense = static_cast<std::uint32_t>(m_objects.size() - 1);
    return id;
}

bool Scene::removeObject(ObjectId id) {
    if (!contains(id)) {
        return false;
    }
    Slot &slot = m_slots[id.index];
    const std::uint32_t dense = slot.dense;
    const std::uint32_t last = static_cast<std::uint32_t>(m_objects.size() - 1);
    if (dense != last) {
        // Fill the hole with the last object and point its slot at the new position.
        m_objects[dense] = std::move(m_objects[last]);
        m_ids[dense] = m_ids[last];
        m_slots[m_ids[dense].index].dense = dense;
    }
    m_objects.pop_back();
    m_ids.pop_back();
    // A slot whose generation would wrap is retired rather than reused, so no id ever becomes valid again.
    if (slot.generation != UINT32_MAX) {
        ++slot.generation;
        m_freeSlots.push_back(id.index);
    }
    return true;
}

bool Scene::contains(ObjectId id) const { return lookup(id) != nullptr; }

const Object *Scene::lookup(ObjectId id) const {
    if (id.index >= m_slots.size()) {
        return nullptr;
    }
    const Slot &slot = m_slots[id.index];
    // A free slot has already moved on to the next generation, so the check also rejects it.
    if (slot.generation != id.generation || slot.dense >= m_ids.size() || m_ids[slot.dense] != id) {
        return nullptr;
    }
    return &m_objects[slot.dense];
}

const Object *Scene::get(ObjectId id) const { return lookup(id); }
Object *Scene::get(ObjectId id) { return const_cast<Object *>(lookup(id)); }

const Object &Scene::at(ObjectId id) const {
    const Object *object = lookup(id);
    if (object == nullptr) {
        throw std::out_of_range("Scene: no object with id " + std::to_string(id.index) + "." +
                                std::to_string(id.generation));
    }
    return *object;
}
Object &Scene::at(ObjectId id) { return const_cast<Object &>(std::as_const(*this).at(id)); }

ObjectId Scene::find(std::string_view name) const {
    for (std::size_t i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i].name() == name) {
            return m_ids[i];
        }
    }
    return {};
}

} // namespace phox
