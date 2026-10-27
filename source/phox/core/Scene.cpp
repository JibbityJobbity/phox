#include "phox/Scene.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phox {

namespace {

std::uint64_t nextMeshId() {
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

} // namespace

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
    return glm::clamp(c, vec3(0.0f), vec3(1.0f));
}

mat4 Transform::matrix() const {
    return glm::translate(mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(mat4(1.0f), scale);
}

Object &Scene::addObject(std::string name, std::shared_ptr<const Mesh> mesh, Material material, Transform transform) {
    m_objects.push_back(std::make_unique<Object>(std::move(name), std::move(mesh), std::move(material), transform));
    return *m_objects.back();
}

Object *Scene::find(std::string_view name) {
    for (const auto &object : m_objects) {
        if (object->name() == name) {
            return object.get();
        }
    }
    return nullptr;
}

} // namespace phox
