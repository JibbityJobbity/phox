// Scene, camera and view extraction, meshlets, the software rasterizer and mesh loading. No GPU needed.
#include "phox/MeshLoader.hpp"
#include "phox/Meshlets.hpp"
#include "phox/RenderView.hpp"
#include "phox/Scene.hpp"
#include "phox/SoftwareRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                                     \
        }                                                                                     \
    } while (0)

using namespace phox;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }
bool near(const vec3 &a, const vec3 &b, float eps = 1e-4f) { return glm::length(a - b) <= eps; }

void projectionMatchesVulkanClipSpace() {
    const mat4 proj = perspective(glm::radians(90.0f), 1.0f, 1.0f, 10.0f);
    const vec4 nearPoint = proj * vec4(0.0f, 0.0f, -1.0f, 1.0f);
    const vec4 farPoint = proj * vec4(0.0f, 0.0f, -10.0f, 1.0f);
    CHECK(near(nearPoint.z / nearPoint.w, 0.0f));
    CHECK(near(farPoint.z / farPoint.w, 1.0f));
    // +y in view space is up the screen, which is -y in Vulkan clip space.
    const vec4 up = proj * vec4(0.0f, 1.0f, -2.0f, 1.0f);
    CHECK(up.y / up.w < 0.0f);
}

void cameraLooksAtItsTarget() {
    Camera camera;
    camera.position = {3.0f, 2.0f, 5.0f};
    camera.lookAt({0.0f, 0.0f, 0.0f});
    CHECK(near(camera.forward(), glm::normalize(-camera.position)));
    // The target ends up straight ahead: on the view-space -Z axis.
    const vec4 target = camera.view() * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(near(target.x, 0.0f) && near(target.y, 0.0f) && target.z < 0.0f);
    CHECK(near(-target.z, glm::length(camera.position)));
}

void transformScalesThenRotatesThenTranslates() {
    Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.rotation = glm::angleAxis(glm::radians(90.0f), vec3(0.0f, 1.0f, 0.0f));
    t.scale = vec3(2.0f);
    // (1, 0, 0) -> scaled (2, 0, 0) -> rotated about +Y to (0, 0, -2) -> translated.
    const vec3 p = vec3(t.matrix() * vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(near(p, vec3(1.0f, 2.0f, 1.0f)));
}

void meshesValidateAndVersion() {
    const Mesh cube = Mesh::cube(2.0f);
    CHECK(cube.triangleCount() == 12);
    CHECK(near(cube.bounds().min, vec3(-1.0f)) && near(cube.bounds().max, vec3(1.0f)));
    const Mesh triangle = Mesh::triangle(1.0f);
    CHECK(triangle.triangleCount() == 1);
    CHECK(near(triangle.bounds().center().x, 0.0f));

    Mesh copy = cube;
    CHECK(copy.id() != cube.id()); // a copy is a different mesh to caches
    const auto version = copy.version();
    copy.setGeometry(triangle.vertices(), triangle.indices());
    CHECK(copy.version() != version && copy.triangleCount() == 1);
    const auto id = copy.id();
    Mesh moved = std::move(copy);
    CHECK(moved.id() == id && copy.id() != id && copy.empty());

    bool threw = false;
    try {
        Mesh bad({Vertex{}}, {0, 0, 5});
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);
}

Scene twoCubeScene() {
    // A red cube 2 units away and a green one 6 units away, both straight ahead, plus one behind the camera.
    Scene scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(1.0f));
    scene.addObject("far", cube, Material{"green", {0.0f, 1.0f, 0.0f, 1.0f}}, Transform{{0.0f, 0.0f, -6.0f}});
    scene.addObject("near", cube, Material{"red", {1.0f, 0.0f, 0.0f, 1.0f}}, Transform{{0.0f, 0.0f, -2.0f}});
    scene.addObject("behind", cube, Material{"blue", {0.0f, 0.0f, 1.0f, 1.0f}}, Transform{{0.0f, 0.0f, 5.0f}});
    scene.addObject("no mesh", nullptr, Material{});
    scene.camera().position = {0.0f, 0.0f, 0.0f};
    scene.light().direction = {0.0f, 0.0f, -1.0f}; // shining straight at the cubes' front faces
    scene.light().ambient = 0.0f;
    return scene;
}

void extractionCullsAndSorts() {
    const Scene scene = twoCubeScene();
    const RenderView view = extractView(scene, {200, 100});
    CHECK(view.draws.size() == 2);
    CHECK(view.culled == 1);
    CHECK(view.draws[0].material->name == "red"); // nearest first
    CHECK(view.draws[1].material->name == "green");
    CHECK(near(view.draws[0].viewDepth, 2.0f) && near(view.draws[1].viewDepth, 6.0f));
    CHECK(near(view.lightDirection, vec3(0.0f, 0.0f, 1.0f)));
    // The aspect ratio comes from the extent.
    CHECK(near(view.proj[1][1] / view.proj[0][0], -2.0f));
}

void frustumTestUsesEveryPlane() {
    const mat4 viewProj = perspective(glm::radians(60.0f), 1.0f, 0.1f, 10.0f);
    const Bounds unit{vec3(-0.5f), vec3(0.5f)};
    const auto at = [&](vec3 p) { return outsideFrustum(viewProj * glm::translate(mat4(1.0f), p), unit); };
    CHECK(!at({0.0f, 0.0f, -3.0f}));
    CHECK(at({0.0f, 0.0f, 3.0f}));    // behind
    CHECK(at({0.0f, 0.0f, -50.0f}));  // beyond the far plane
    CHECK(at({20.0f, 0.0f, -3.0f}));  // right
    CHECK(at({-20.0f, 0.0f, -3.0f})); // left
    CHECK(at({0.0f, 20.0f, -3.0f}));  // above
    CHECK(at({0.0f, -20.0f, -3.0f})); // below
    CHECK(!at({0.0f, 0.0f, -10.2f})); // straddling the far plane is still drawn
}

void rasterizerDepthTestsAndLights() {
    const Scene scene = twoCubeScene();
    const CpuImage image = rasterize(extractView(scene, {200, 100}));
    CHECK(image.width == 200 && image.height == 100);
    // The near red cube covers the far green one in the middle; its front face is fully lit.
    const auto centre = image.pixel(100, 50);
    CHECK(centre[0] == 255 && centre[1] == 0 && centre[2] == 0);
    // Corners show the background (0.1, 0.2, 0.3).
    const auto corner = image.pixel(0, 0);
    CHECK(corner[0] == 26 && corner[1] == 51 && corner[2] == 77);
}

// The geometric normal (from the winding) of triangle @p t of @p indices.
vec3 faceNormal(const Mesh &mesh, const std::vector<std::uint32_t> &indices, std::size_t t) {
    const vec3 a = mesh.vertices()[indices[3 * t]].position;
    const vec3 b = mesh.vertices()[indices[3 * t + 1]].position;
    const vec3 c = mesh.vertices()[indices[3 * t + 2]].position;
    return glm::cross(b - a, c - a);
}

void torusIsClosedAndWoundOutwards() {
    const Mesh torus = Mesh::torus(1.0f, 0.25f, 24, 12);
    CHECK(torus.triangleCount() == 24 * 12 * 2);
    CHECK(near(torus.bounds().min, vec3(-1.25f, -0.25f, -1.25f), 1e-3f));
    CHECK(near(torus.bounds().max, vec3(1.25f, 0.25f, 1.25f), 1e-3f));
    for (std::size_t t = 0; t < torus.triangleCount(); ++t) {
        // Counter-clockwise from outside: the winding agrees with the vertex normals.
        CHECK(glm::dot(faceNormal(torus, torus.indices(), t), torus.vertices()[torus.indices()[3 * t]].normal) > 0.0f);
    }
}

// A triangle as its three indices rotated so the smallest comes first: equal for the same triangle with
// the same winding, wherever it starts.
std::array<std::uint32_t, 3> canonical(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (b < a && b < c) return {b, c, a};
    if (c < a && c < b) return {c, a, b};
    return {a, b, c};
}

void meshletsCoverEveryTriangleOnce() {
    const Mesh torus = Mesh::torus(1.0f, 0.3f, 64, 32);
    const std::vector<std::uint32_t> indices = optimizeIndexOrder(torus);
    CHECK(indices.size() == torus.indices().size());
    const MeshletMesh meshlets = buildMeshlets(torus, indices);
    CHECK(meshlets.meshlets.size() >= torus.triangleCount() / kMeshletMaxTriangles);
    CHECK(meshlets.triangleCount() == torus.triangleCount());

    std::vector<std::array<std::uint32_t, 3>> expected, actual;
    for (std::size_t i = 0; i < torus.indices().size(); i += 3) {
        expected.push_back(canonical(torus.indices()[i], torus.indices()[i + 1], torus.indices()[i + 2]));
    }
    for (const Meshlet &m : meshlets.meshlets) {
        CHECK(m.vertexCount > 0 && m.vertexCount <= kMeshletMaxVertices);
        CHECK(m.triangleCount > 0 && m.triangleCount <= kMeshletMaxTriangles);
        CHECK(m.vertexOffset + m.vertexCount <= meshlets.data.size());
        CHECK(m.triangleOffset + m.triangleCount <= meshlets.data.size());
        std::array<std::uint32_t, 3> vertex{};
        for (std::uint32_t t = 0; t < m.triangleCount; ++t) {
            const std::uint32_t packed = meshlets.data[m.triangleOffset + t];
            CHECK(packed >> 24 == 0);
            for (std::uint32_t k = 0; k < 3; ++k) {
                const std::uint32_t local = (packed >> (8 * k)) & 0xFF;
                CHECK(local < m.vertexCount);
                vertex[k] = meshlets.data[m.vertexOffset + local];
                // The bounding sphere holds every vertex.
                CHECK(glm::length(torus.vertices()[vertex[k]].position - vec3(m.sphere)) <= m.sphere.w + 1e-4f);
            }
            actual.push_back(canonical(vertex[0], vertex[1], vertex[2]));
        }
    }
    // The same triangles with the same winding, each exactly once.
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    CHECK(actual == expected);
}

void coneCullingIsConservative() {
    // Dense enough that meshlets are nearly flat, as in real models: their normal cones are then narrow.
    const Mesh torus = Mesh::torus(1.0f, 0.3f, 128, 64);
    const MeshletMesh meshlets = buildMeshlets(torus, optimizeIndexOrder(torus));
    const std::array<vec3, 4> cameras{{{0.0f, 0.0f, 4.0f}, {3.0f, 2.0f, 0.5f}, {0.0f, 5.0f, 0.0f}, {-1.5f, -0.2f, -2.5f}}};
    for (const vec3 &camera : cameras) {
        std::size_t culled = 0;
        for (const Meshlet &m : meshlets.meshlets) {
            if (!backFacing(m, camera)) {
                continue;
            }
            ++culled;
            // Every triangle of a culled meshlet faces away from the camera.
            for (std::uint32_t t = 0; t < m.triangleCount; ++t) {
                const std::uint32_t packed = meshlets.data[m.triangleOffset + t];
                std::vector<std::uint32_t> triangle;
                for (std::uint32_t k = 0; k < 3; ++k) {
                    triangle.push_back(meshlets.data[m.vertexOffset + ((packed >> (8 * k)) & 0xFF)]);
                }
                const vec3 a = torus.vertices()[triangle[0]].position;
                CHECK(glm::dot(faceNormal(torus, triangle, 0), camera - a) <= 1e-6f);
            }
        }
        // From outside, a good share of a smooth closed mesh faces away.
        // From outside, a good share of a smooth closed mesh faces away (at most half could).
        CHECK(culled > meshlets.meshlets.size() / 5);
    }
}

void frustumPlanesBoundClipSpace() {
    // Looking down -Z with a 90 degree field of view: the side planes are at 45 degrees.
    const mat4 viewProj = perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const std::array<vec4, 6> planes = frustumPlanes(viewProj);
    for (const vec4 &plane : planes) {
        CHECK(near(glm::length(vec3(plane)), 1.0f));
    }
    CHECK(!sphereOutsideFrustum(planes, {0.0f, 0.0f, -5.0f}, 0.0f));
    CHECK(!sphereOutsideFrustum(planes, {4.9f, -4.9f, -5.0f}, 0.0f));
    // Each plane: a point outside it, by d; a sphere of radius d around it just reaches back inside.
    const float side = std::sqrt(0.5f); // distance of (+-6, -5) from the plane x = +-z
    const std::array<std::pair<vec3, float>, 6> outside{{{{-6.0f, 0.0f, -5.0f}, side},
                                                         {{6.0f, 0.0f, -5.0f}, side},
                                                         {{0.0f, -6.0f, -5.0f}, side},
                                                         {{0.0f, 6.0f, -5.0f}, side},
                                                         {{0.0f, 0.0f, -0.05f}, 0.05f},
                                                         {{0.0f, 0.0f, -10.5f}, 0.5f}}};
    for (const auto &[point, distance] : outside) {
        CHECK(sphereOutsideFrustum(planes, point, 0.0f));
        CHECK(sphereOutsideFrustum(planes, point, 0.9f * distance));
        CHECK(!sphereOutsideFrustum(planes, point, 1.1f * distance));
    }
}

void projectedSpheresBoundTheirSilhouette() {
    const mat4 proj = perspective(glm::radians(70.0f), 1.6f, 0.1f, 50.0f);
    // Texture coordinates of a view-space point, as the GPU maps them (y down).
    const auto screen = [&](const vec3 &p) {
        const vec4 clip = proj * vec4(p, 1.0f);
        return vec2(clip) / clip.w * 0.5f + 0.5f;
    };
    const std::array<std::pair<vec3, float>, 5> spheres{{{{0.0f, 0.0f, -5.0f}, 1.0f},
                                                         {{3.0f, -1.5f, -4.0f}, 0.5f},
                                                         {{-6.0f, 4.0f, -3.0f}, 1.5f}, // largely off screen
                                                         {{0.5f, 0.2f, -1.0f}, 0.8f},  // close: a wide rectangle
                                                         {{-1.0f, 2.0f, -20.0f}, 0.1f}}};
    for (const auto &[center, radius] : spheres) {
        vec4 bounds;
        CHECK(projectSphere(center, radius, proj, bounds));
        CHECK(bounds.x < bounds.z && bounds.y < bounds.w);
        // Every point of the sphere projects inside; points on its silhouette reach every edge.
        vec4 reached{2.0f, 2.0f, -2.0f, -2.0f};
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j <= 32; ++j) {
                const float theta = glm::two_pi<float>() * static_cast<float>(i) / 64.0f;
                const float phi = glm::pi<float>() * static_cast<float>(j) / 32.0f;
                const vec3 p = center + radius * vec3(std::sin(phi) * std::cos(theta), std::cos(phi),
                                                      std::sin(phi) * std::sin(theta));
                const vec2 uv = screen(p);
                CHECK(uv.x >= bounds.x - 1e-4f && uv.x <= bounds.z + 1e-4f);
                CHECK(uv.y >= bounds.y - 1e-4f && uv.y <= bounds.w + 1e-4f);
                reached = {std::min(reached.x, uv.x), std::min(reached.y, uv.y), std::max(reached.z, uv.x),
                           std::max(reached.w, uv.y)};
            }
        }
        const float tolerance = 0.02f * std::max(bounds.z - bounds.x, bounds.w - bounds.y);
        CHECK(glm::all(glm::lessThanEqual(glm::abs(reached - bounds), vec4(tolerance))));
    }
    vec4 bounds;
    CHECK(!projectSphere({0.0f, 0.0f, -0.5f}, 0.45f, proj, bounds)); // reaches the near plane
    CHECK(!projectSphere({0.0f, 0.0f, 2.0f}, 0.5f, proj, bounds));   // behind the camera
}

void depthPyramidIsPowersOfTwo() {
    CHECK((depthPyramidExtent({1920, 1080}) == Extent2D{1024, 1024}));
    CHECK((depthPyramidExtent({320, 240}) == Extent2D{256, 128}));
    CHECK((depthPyramidExtent({512, 1}) == Extent2D{512, 1}));
    CHECK(depthPyramidLevels({1024, 1024}) == 11);
    CHECK(depthPyramidLevels({256, 128}) == 9);
    CHECK(depthPyramidLevels({1, 1}) == 1);
}

void objectsHaveTheirOwnIds() {
    Scene scene;
    Object &a = scene.addObject("a", nullptr, Material{});
    Object &b = scene.addObject("b", nullptr, Material{});
    CHECK(a.id() != 0 && a.id() != b.id());
    const Object copy = a;
    CHECK(copy.id() != a.id() && copy.name() == "a");
    const std::uint64_t id = b.id();
    b = a;
    CHECK(b.id() == id && b.name() == "a");
}

void singleSidedMaterialsCullBackFaces() {
    // The triangle faces +Z; the camera looks at its back from -Z.
    Scene scene;
    auto triangle = std::make_shared<Mesh>(Mesh::triangle(1.0f));
    Object &object = scene.addObject("triangle", triangle, Material{"red", {1.0f, 0.0f, 0.0f, 1.0f}});
    scene.camera().position = {0.0f, 0.0f, -3.0f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    const auto centre = [&] { return rasterize(extractView(scene, {64, 64})).pixel(32, 32); };
    CHECK(extractView(scene, {64, 64}).draws.at(0).cullBackFaces == false);
    CHECK(centre()[0] > 0); // double-sided: the back face is drawn

    object.material().doubleSided = false;
    CHECK(extractView(scene, {64, 64}).draws.at(0).cullBackFaces);
    CHECK(centre()[0] == 26); // background

    // A mirroring transform turns the front face into the back face, so nothing is culled.
    object.transform().scale = {-1.0f, 1.0f, 1.0f};
    CHECK(extractView(scene, {64, 64}).draws.at(0).cullBackFaces == false);
}

void loadsObjFiles() {
    const auto path = std::filesystem::temp_directory_path() / "phox_quad_test.obj";
    {
        std::ofstream obj(path);
        obj << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nf 1 2 3 4\n"; // one quad, no normals
    }
    const Mesh quad = loadMesh(path);
    std::filesystem::remove(path);
    CHECK(quad.triangleCount() == 2);
    CHECK(quad.vertices().size() == 4);
    CHECK(near(glm::abs(quad.vertices()[0].normal), vec3(0.0f, 0.0f, 1.0f))); // generated
    CHECK(near(quad.bounds().min, vec3(-1.0f, -1.0f, 0.0f)));

    bool threw = false;
    try {
        loadMesh(std::filesystem::temp_directory_path() / "phox_missing_file.obj");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    projectionMatchesVulkanClipSpace();
    cameraLooksAtItsTarget();
    transformScalesThenRotatesThenTranslates();
    meshesValidateAndVersion();
    extractionCullsAndSorts();
    frustumTestUsesEveryPlane();
    rasterizerDepthTestsAndLights();
    torusIsClosedAndWoundOutwards();
    meshletsCoverEveryTriangleOnce();
    coneCullingIsConservative();
    frustumPlanesBoundClipSpace();
    singleSidedMaterialsCullBackFaces();
    projectedSpheresBoundTheirSilhouette();
    depthPyramidIsPowersOfTwo();
    objectsHaveTheirOwnIds();
    loadsObjFiles();
    std::cout << "render view tests passed" << std::endl;
    return 0;
}
