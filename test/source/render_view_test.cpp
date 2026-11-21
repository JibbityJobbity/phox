// Scene, camera and view extraction, meshlets, the software rasterizer and mesh loading. No GPU needed.
#include "phox/GltfLoader.hpp"
#include "phox/MeshLoader.hpp"
#include "phox/Meshlets.hpp"
#include "phox/RenderView.hpp"
#include "phox/Scene.hpp"
#include "phox/ShaderInterface.hpp"
#include "phox/SoftwareRasterizer.hpp"
#include "phox/Texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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

void objectIdsSurviveRemoval() {
    Scene scene;
    const auto [a, b, c] = scene.addObjects({{.name = "a"}, {.name = "b"}, {.name = "c"}});
    CHECK(a.valid() && !(a == b) && !ObjectId{}.valid() && !scene.contains(ObjectId{}));
    CHECK(scene.objectIds()[0] == a && scene.objectIds()[1] == b && scene.objectIds()[2] == c);

    // Removing from the middle moves another object into the hole; every other id still finds its object.
    CHECK(scene.removeObject(a));
    CHECK(!scene.contains(a) && scene.get(a) == nullptr);
    CHECK(!scene.removeObject(a));
    CHECK(scene.objectCount() == 2);
    CHECK(scene.at(b).name() == "b" && scene.at(c).name() == "c");
    for (std::size_t i = 0; i < scene.objectCount(); ++i) {
        CHECK(scene.get(scene.objectIds()[i]) == &scene.objects()[i]);
    }

    // The freed slot is reused with a new generation, so the stale id stays stale.
    const ObjectId d = scene.addObject({.name = "d"});
    CHECK(d.index == a.index && d.generation != a.generation);
    CHECK(!scene.contains(a) && scene.at(d).name() == "d");
    bool threw = false;
    try {
        scene.at(a);
    } catch (const std::out_of_range &) {
        threw = true;
    }
    CHECK(threw);

    // Ids stay meaningful in a copy of the scene.
    const Scene copy = scene;
    CHECK(copy.at(c).name() == "c" && !copy.contains(a));

    CHECK(scene.removeObject(c) && scene.removeObject(b) && scene.removeObject(d));
    CHECK(scene.objectCount() == 0 && !scene.contains(b));
}

Scene twoCubeScene() {
    // A red cube 2 units away and a green one 6 units away, both straight ahead, plus one behind the camera.
    Scene scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(1.0f));
    scene.addObjects({
        {.mesh = cube, .material = {.albedo = {0.0f, 1.0f, 0.0f, 1.0f}}, .transform = {.position = {0.0f, 0.0f, -6.0f}}}, // far
        {.mesh = cube, .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}, .transform = {.position = {0.0f, 0.0f, -2.0f}}}, // near
        {.mesh = cube, .material = {.albedo = {0.0f, 0.0f, 1.0f, 1.0f}}, .transform = {.position = {0.0f, 0.0f, 5.0f}}}, // behind
        {}, // no mesh
    });
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
    CHECK(view.draws[0].material->albedo.r == 1.0f); // nearest first
    CHECK(view.draws[1].material->albedo.g == 1.0f);
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
    // The near red cube covers the far green one in the middle; its front face is fully lit, with a
    // white (dielectric) highlight adding the same to green and blue.
    const auto centre = image.pixel(100, 50);
    CHECK(centre[0] == 255 && centre[1] == centre[2] && centre[1] > 0 && centre[1] < 160);
    // Corners show the background, sRGB (0.1, 0.2, 0.3): stored linear, encoded again on output.
    const auto corner = image.pixel(0, 0);
    CHECK(std::abs(corner[0] - 26) <= 1 && std::abs(corner[1] - 51) <= 1 && std::abs(corner[2] - 77) <= 1);
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
    const auto [idA, idB] = scene.addObjects({{.name = "a"}, {.name = "b"}});
    Object &a = scene.at(idA);
    Object &b = scene.at(idB);
    CHECK(a.id() != 0 && a.id() != b.id());
    const Object copy = a;
    CHECK(copy.id() != a.id() && copy.name() == "a");
    const std::uint64_t id = b.id();
    b = a;
    CHECK(b.id() == id && b.name() == "a");

    // The scene moves objects around (growing, filling a removed one's slot); they keep their ids.
    Scene grown;
    const auto [first, last] = grown.addObjects({{}, {}});
    const std::uint64_t lastId = grown.at(last).id();
    for (int i = 0; i < 100; ++i) {
        grown.addObject({});
    }
    CHECK(grown.at(last).id() == lastId);
    const ObjectId end = grown.objectIds().back();
    const std::uint64_t endId = grown.at(end).id();
    grown.removeObject(first); // the last object moves into its place
    CHECK(grown.at(end).id() == endId && grown.at(last).id() == lastId);
}

void singleSidedMaterialsCullBackFaces() {
    // The triangle faces +Z; the camera looks at its back from -Z.
    Scene scene;
    auto triangle = std::make_shared<Mesh>(Mesh::triangle(1.0f));
    Object &object = scene.at(scene.addObject({.mesh = triangle, .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}}}));
    scene.camera().position = {0.0f, 0.0f, -3.0f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    const auto centre = [&] { return rasterize(extractView(scene, {64, 64})).pixel(32, 32); };
    CHECK(extractView(scene, {64, 64}).draws.at(0).cullBackFaces == false);
    CHECK(centre()[0] > 0); // double-sided: the back face is drawn

    object.material().doubleSided = false;
    CHECK(extractView(scene, {64, 64}).draws.at(0).cullBackFaces);
    CHECK(std::abs(centre()[0] - 26) <= 1); // background

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

void texturesBuildMipChains() {
    CHECK(mipCountFor(1, 1) == 1 && mipCountFor(256, 256) == 9 && mipCountFor(320, 240) == 9 && mipCountFor(1, 64) == 7);
    // Black and white sRGB texels average to linear 0.5, which is sRGB 188, not 128.
    const Texture checker = Texture::checker("bw", 4, 4, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec4(1.0f));
    CHECK(checker.mipCount() == 3);
    CHECK(checker.mip(1).width == 2 && checker.mip(2).width == 1 && checker.mip(2).rgba.size() == 4);
    CHECK(checker.mip(2).rgba[0] == 188 && checker.mip(2).rgba[3] == 255);
    // Linear data averages as stored.
    const Texture data = Texture::checker("data", 2, 2, vec4(0.0f), vec4(1.0f), TextureEncoding::Linear);
    CHECK(data.mip(1).rgba[0] == 128);
    // Normal maps are renormalised: two tilted normals average to straight up, not to a shorter vector.
    std::vector<std::uint8_t> tilted;
    for (const vec3 n : {glm::normalize(vec3(1, 0, 1)), glm::normalize(vec3(-1, 0, 1))}) {
        for (int c = 0; c < 3; ++c) tilted.push_back(static_cast<std::uint8_t>(std::lround((n[c] * 0.5f + 0.5f) * 255.0f)));
        tilted.push_back(255);
    }
    const Texture normals("normals", 2, 1, tilted, TextureEncoding::Normal);
    CHECK(normals.mip(1).rgba[2] == 255);
    // Odd sizes halve down to 1 x 1.
    const Texture odd("odd", 5, 3, std::vector<std::uint8_t>(5 * 3 * 4, 7), TextureEncoding::Linear);
    CHECK(odd.mipCount() == 3 && odd.mip(1).width == 2 && odd.mip(1).height == 1 && odd.mip(2).rgba[0] == 7);
    bool threw = false;
    try {
        Texture bad("bad", 2, 2, std::vector<std::uint8_t>(3), TextureEncoding::Linear);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);
    // A copy is a different texture to caches.
    const Texture copy = checker;
    CHECK(copy.id() != checker.id() && copy.mipCount() == checker.mipCount());
}

void texturesSampleLikeTheGpu() {
    // 2 x 1: a black and a white texel. Texel centres are at u = 0.25 and 0.75.
    std::vector<std::uint8_t> rgba{0, 0, 0, 255, 255, 255, 255, 255};
    const Texture clamp("clamp", 2, 1, rgba, TextureEncoding::Linear,
                        SamplerDesc{.wrapU = TextureWrap::ClampToEdge, .wrapV = TextureWrap::ClampToEdge});
    CHECK(near(clamp.sample({0.25f, 0.5f}).r, 0.0f) && near(clamp.sample({0.75f, 0.5f}).r, 1.0f));
    CHECK(near(clamp.sample({0.5f, 0.5f}).r, 0.5f));
    CHECK(near(clamp.sample({0.0f, 0.5f}).r, 0.0f)); // clamped, not wrapped round to the white texel
    const Texture repeat("repeat", 2, 1, rgba, TextureEncoding::Linear);
    CHECK(near(repeat.sample({0.0f, 0.5f}).r, 0.5f)); // halfway between the last and the first texel
    CHECK(near(repeat.sample({1.25f, 0.5f}).r, 0.0f));
    // sRGB texels are decoded before filtering.
    const Texture srgbTexture("srgb", 2, 1, {188, 188, 188, 255, 188, 188, 188, 255}, TextureEncoding::Srgb);
    CHECK(near(srgbTexture.sample({0.5f, 0.5f}).r, 0.5f, 0.01f));
    const Texture nearest("nearest", 2, 1, rgba, TextureEncoding::Linear, SamplerDesc{.magFilter = TextureFilter::Nearest});
    CHECK(near(nearest.sample({0.49f, 0.5f}).r, 0.0f) && near(nearest.sample({0.51f, 0.5f}).r, 1.0f));
}

void meshesGenerateTangents() {
    // The plane's u runs along +x and v along +z, so its tangent is +x; increasing v (+z) is opposite to
    // cross(normal, tangent) = cross(+y, +x) = -z, so the handedness is -1.
    const Mesh plane = Mesh::plane(2.0f);
    for (const Vertex &v : plane.vertices()) {
        CHECK(near(vec3(v.tangent), vec3(1.0f, 0.0f, 0.0f)) && v.tangent.w == -1.0f);
    }
    // Generated tangents are unit length and perpendicular to the normal, everywhere on a torus.
    const Mesh torus = Mesh::torus(1.0f, 0.3f, 16, 8);
    for (const Vertex &v : torus.vertices()) {
        CHECK(near(glm::length(vec3(v.tangent)), 1.0f, 1e-3f) && std::abs(glm::dot(vec3(v.tangent), v.normal)) < 1e-3f);
        CHECK(v.tangent.w == 1.0f || v.tangent.w == -1.0f);
    }
    // Without usable texture coordinates a vertex still gets some unit tangent perpendicular to its normal.
    const Mesh flat({{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {0, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 0}}},
                    {0, 1, 2});
    for (const Vertex &v : flat.vertices()) {
        CHECK(near(glm::length(vec3(v.tangent)), 1.0f, 1e-3f) && std::abs(v.tangent.z) < 1e-3f);
    }
    // Given tangents are kept.
    const Mesh given({{{0, 0, 0}, {0, 0, 1}, {0, 0}, {0, 1, 0, 1}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}, {0, 1, 0, 1}},
                      {{0, 1, 0}, {0, 0, 1}, {0, 1}, {0, 1, 0, 1}}},
                     {0, 1, 2});
    CHECK(near(vec3(given.vertices()[0].tangent), vec3(0.0f, 1.0f, 0.0f)));
}

void sphereIsClosedAndMatchesItsTangents() {
    const Mesh sphere = Mesh::sphere(0.5f, 16, 8);
    CHECK(near(sphere.bounds().min, vec3(-0.5f), 1e-3f) && near(sphere.bounds().max, vec3(0.5f), 1e-3f));
    // No degenerate triangles at the poles: 2 per quad, minus one per quad in the top and bottom rows.
    CHECK(sphere.triangleCount() == 16 * 8 * 2 - 2 * 16);
    for (std::size_t t = 0; t < sphere.triangleCount(); ++t) {
        CHECK(glm::dot(faceNormal(sphere, sphere.indices(), t), sphere.vertices()[sphere.indices()[3 * t]].normal) > 0.0f);
    }
    // The analytic tangents agree with what generateTangents works out from the texture coordinates
    // (away from the poles, where every direction is a tangent).
    std::vector<Vertex> generated = sphere.vertices();
    for (Vertex &v : generated) v.tangent = vec4(0.0f);
    generateTangents(generated, sphere.indices());
    for (std::size_t i = 0; i < generated.size(); ++i) {
        const Vertex &v = sphere.vertices()[i];
        if (std::abs(v.normal.y) > 0.9f) {
            continue;
        }
        CHECK(glm::dot(vec3(generated[i].tangent), vec3(v.tangent)) > 0.95f && generated[i].tangent.w == v.tangent.w);
    }
}

void materialsPackForShaders() {
    Material material{.name = "m", .albedo = {0.5f, 0.25f, 1.0f, 0.5f}, .metallic = 0.75f, .roughness = 0.3f,
                      .emissive = {1.0f, 2.0f, 3.0f}, .alphaMode = AlphaMode::Mask, .alphaCutoff = 0.4f};
    MaterialData data = materialData(material);
    CHECK(data.baseColor == material.albedo && data.emissive == vec4(1.0f, 2.0f, 3.0f, 0.0f));
    CHECK(data.metallic == 0.75f && data.roughness == 0.3f && data.alphaCutoff == 0.4f);
    CHECK(data.flags == kMaterialAlphaMask && data.baseColorTexture == kNoTexture && data.emissiveTexture == kNoTexture);
    material.alphaMode = AlphaMode::Blend;
    CHECK(materialData(material).flags == kMaterialAlphaBlend);
    material.alphaMode = AlphaMode::Opaque;
    CHECK(materialData(material).flags == 0);
}

void blendedItemsAreDrawnLastAndFarthestFirst() {
    Scene scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(1.0f));
    const Material glass{.albedo = {0.0f, 0.0f, 1.0f, 0.5f}, .alphaMode = AlphaMode::Blend};
    scene.addObjects({
        {.mesh = cube, .material = glass, .transform = {.position = {0.0f, 0.0f, -2.0f}}},
        {.mesh = cube, .material = glass, .transform = {.position = {0.0f, 0.0f, -5.0f}}},
        // a red wall
        {.mesh = cube,
         .material = {.albedo = {1.0f, 0.0f, 0.0f, 1.0f}},
         .transform = {.position = {0.0f, 0.0f, -8.0f}, .scale = {4.0f, 4.0f, 0.1f}}},
    });
    scene.camera().position = {0.0f, 0.0f, 0.0f};
    scene.camera().lookAt({0.0f, 0.0f, -1.0f});
    const RenderView view = extractView(scene, {64, 64});
    CHECK(view.draws.size() == 1 && view.transparent.size() == 2 && view.itemCount() == 3);
    CHECK(view.transparent[0].viewDepth > view.transparent[1].viewDepth);
    // Blended over the red wall, the two half-transparent blue cubes leave some red showing.
    const auto centre = rasterize(view).pixel(32, 32);
    CHECK(centre[2] > centre[1] && centre[0] > 0 && centre[0] < 200);
}

void maskedTexelsAreCut() {
    // A white quad whose texture is transparent on its left half, in front of a red wall.
    Scene scene;
    std::vector<std::uint8_t> rgba{255, 255, 255, 0, 255, 255, 255, 255};
    auto mask = std::make_shared<Texture>("mask", 2, 1, rgba, TextureEncoding::Srgb,
                                          SamplerDesc{.magFilter = TextureFilter::Nearest}, false);
    auto quad = std::make_shared<Mesh>(Mesh::plane(2.0f));
    const quat upright = glm::angleAxis(glm::half_pi<float>(), vec3(1, 0, 0));
    scene.addObjects({
        // the cutout
        {.mesh = quad,
         .material = {.albedo = vec4(1.0f), .alphaMode = AlphaMode::Mask, .baseColorTexture = mask},
         .transform = {.position = {0.0f, 0.0f, -2.0f}, .rotation = upright}},
        // the wall
        {.mesh = std::make_shared<Mesh>(Mesh::cube(1.0f)),
         .material = {.albedo = {1, 0, 0, 1}},
         .transform = {.position = {0.0f, 0.0f, -6.0f}, .scale = {8.0f, 8.0f, 0.1f}}},
    });
    scene.camera().position = {0.0f, 0.0f, 0.0f};
    scene.camera().lookAt({0.0f, 0.0f, -1.0f});
    const CpuImage image = rasterize(extractView(scene, {64, 64}));
    CHECK(image.pixel(20, 32)[1] < 100);  // left: cut away, the red wall shows
    CHECK(image.pixel(44, 32)[1] > 200); // right: the white quad
}

// A small glTF file with a node hierarchy, a mesh used twice, a primitive without normals or material,
// and a textured, alpha-masked material with an emissive strength (embedded PNG: red, green / blue, and
// white at half alpha). It also lists an extension Phox ignores.
constexpr const char *kTestGltf = R"gltf({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_emissive_strength", "KHR_materials_clearcoat"],
  "scene": 0,
  "scenes": [{"nodes": [0, 2, 3]}],
  "nodes": [
    {"name": "parent", "translation": [0, 0, -2], "children": [1]},
    {"name": "child", "mesh": 0, "scale": [2, 2, 2]},
    {"name": "tri", "mesh": 1, "rotation": [0, 0.70710678, 0, 0.70710678]},
    {"name": "again", "mesh": 0, "translation": [3, 0, 0]}
  ],
  "meshes": [
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 4, "TEXCOORD_0": 1}, "indices": 2, "material": 0}]},
    {"primitives": [{"attributes": {"POSITION": 3}}]}
  ],
  "materials": [{
    "name": "painted",
    "pbrMetallicRoughness": {"baseColorFactor": [0.5, 1, 1, 1], "baseColorTexture": {"index": 0},
                             "metallicFactor": 0.25, "roughnessFactor": 0.75},
    "emissiveFactor": [1, 0.5, 0],
    "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2}},
    "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true
  }],
  "textures": [{"source": 0, "sampler": 0}],
  "samplers": [{"magFilter": 9728, "minFilter": 9729, "wrapS": 33071, "wrapT": 33648}],
  "images": [{"uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAE0lEQVR4nGP4z8DwHwyBNAg0AABJSQl4KKDbdwAAAABJRU5ErkJggg=="}],
  "buffers": [{"byteLength": 180, "uri": "data:application/octet-stream;base64,AACAvwAAgL8AAAAAAACAPwAAgL8AAAAAAACAPwAAgD8AAAAAAACAvwAAgD8AAAAAAAAAAAAAgD8AAIA/AACAPwAAgD8AAAAAAAAAAAAAAAAAAAEAAgAAAAIAAwAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 32},
    {"buffer": 0, "byteOffset": 80, "byteLength": 12},
    {"buffer": 0, "byteOffset": 96, "byteLength": 36},
    {"buffer": 0, "byteOffset": 132, "byteLength": 48}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 4, "componentType": 5126, "count": 4, "type": "VEC3"}
  ]
})gltf";

void loadsGltfFiles() {
    const auto path = std::filesystem::temp_directory_path() / "phox_gltf_test.gltf";
    {
        std::ofstream file(path);
        file << kTestGltf;
    }
    Scene scene;
    const GltfImport import = loadGltf(path, scene);
    std::filesystem::remove(path);

    CHECK(import.objects.size() == 3 && scene.objectCount() == 3);
    CHECK(import.meshes == 2 && import.materials == 2 && import.textures == 1); // the quad mesh is shared
    CHECK(import.warnings.size() == 1 && import.warnings[0].find("KHR_materials_clearcoat") != std::string::npos);
    // World bounds: the child quad (scaled by 2, at z = -2), the triangle, the second quad at x = 3.
    CHECK(near(import.bounds.min, vec3(-2.0f, -2.0f, -2.0f)) && near(import.bounds.max, vec3(4.0f, 2.0f, 0.0f)));

    const Object &child = scene.at(import.objects[0]);
    CHECK(child.name() == "child");
    CHECK(near(child.transform().position, vec3(0.0f, 0.0f, -2.0f)) && near(child.transform().scale, vec3(2.0f)));
    const Mesh &quad = *child.mesh();
    CHECK(quad.vertices().size() == 4 && quad.triangleCount() == 2);
    CHECK(near(quad.vertices()[0].normal, vec3(0.0f, 0.0f, 1.0f)) && near(quad.vertices()[2].uv.x, 1.0f));
    CHECK(near(vec3(quad.vertices()[0].tangent), vec3(1.0f, 0.0f, 0.0f))); // generated: u runs along +x
    CHECK(scene.at(import.objects[2]).mesh() == child.mesh());

    const Material &painted = child.material();
    CHECK(painted.name == "painted" && painted.albedo == vec4(0.5f, 1.0f, 1.0f, 1.0f));
    CHECK(painted.metallic == 0.25f && painted.roughness == 0.75f && near(painted.emissive, vec3(2.0f, 1.0f, 0.0f)));
    CHECK(painted.alphaMode == AlphaMode::Mask && near(painted.alphaCutoff, 0.3f) && painted.doubleSided);
    CHECK(!painted.normalTexture && !painted.metallicRoughnessTexture);
    const Texture &texture = *painted.baseColorTexture;
    CHECK(texture.width() == 2 && texture.height() == 2 && texture.encoding() == TextureEncoding::Srgb);
    CHECK(texture.mipCount() == 1); // the sampler's minification filter does not use mips
    CHECK(texture.sampler().magFilter == TextureFilter::Nearest && texture.sampler().wrapU == TextureWrap::ClampToEdge &&
          texture.sampler().wrapV == TextureWrap::MirroredRepeat);
    const auto &texels = texture.mip(0).rgba;
    CHECK(texels[0] == 255 && texels[1] == 0 && texels[4 + 1] == 255 && texels[8 + 2] == 255 && texels[12 + 3] == 128);

    // No normals: flat ones, one set of vertices per triangle. No material: glTF's default.
    const Object &tri = scene.at(import.objects[1]);
    CHECK(tri.mesh()->vertices().size() == 3 && near(tri.mesh()->vertices()[1].normal, vec3(0.0f, 0.0f, 1.0f)));
    CHECK(near(tri.transform().rotation * vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f)));
    CHECK(tri.material().metallic == 1.0f && tri.material().roughness == 1.0f && !tri.material().doubleSided);

    bool threw = false;
    try {
        loadGltf(std::filesystem::temp_directory_path() / "phox_no_such_file.gltf", scene);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
}

// The triangles (as mesh vertex indices) of the meshlets lodSelected picks for @p camera.
std::vector<std::array<std::uint32_t, 3>> selectedTriangles(const MeshletMesh &lod, const vec3 &camera,
                                                           float pixelsPerUnit, float threshold) {
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (const Meshlet &meshlet : lod.meshlets) {
        if (!lodSelected(meshlet, camera, pixelsPerUnit, threshold)) {
            continue;
        }
        for (std::uint32_t t = 0; t < meshlet.triangleCount; ++t) {
            const std::uint32_t packed = lod.data[meshlet.triangleOffset + t];
            std::array<std::uint32_t, 3> triangle;
            for (int k = 0; k < 3; ++k) triangle[k] = lod.data[meshlet.vertexOffset + ((packed >> (8 * k)) & 0xFFu)];
            triangles.push_back(triangle);
        }
    }
    return triangles;
}

// Edges (by vertex position) used by other than exactly one triangle in each direction: zero for a
// closed surface without cracks or overlaps.
std::size_t badEdges(const Mesh &mesh, const std::vector<std::array<std::uint32_t, 3>> &triangles) {
    std::map<std::array<float, 3>, std::uint32_t> ids;
    const auto id = [&](std::uint32_t v) {
        const vec3 &p = mesh.vertices()[v].position;
        return ids.try_emplace({p.x, p.y, p.z}, static_cast<std::uint32_t>(ids.size())).first->second;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (const auto &t : triangles) {
        const std::uint32_t a = id(t[0]), b = id(t[1]), c = id(t[2]);
        if (a == b || b == c || a == c) {
            continue; // degenerate: no area, no edges
        }
        ++directed[{a, b}];
        ++directed[{b, c}];
        ++directed[{c, a}];
    }
    std::size_t bad = 0;
    for (const auto &[edge, count] : directed) {
        const auto reverse = directed.find({edge.second, edge.first});
        if (count != 1 || reverse == directed.end() || reverse->second != 1) ++bad;
    }
    return bad;
}

void clusterLodIsCrackFree() {
    // A dense closed mesh, as in the "field" scene, with texture seams (the torus duplicates its seam vertices).
    const Mesh torus = Mesh::torus(0.6f, 0.22f, 128, 64);
    std::vector<std::array<std::uint32_t, 3>> original;
    for (std::size_t i = 0; i + 2 < torus.indices().size(); i += 3) {
        original.push_back({torus.indices()[i], torus.indices()[i + 1], torus.indices()[i + 2]});
    }
    CHECK(badEdges(torus, original) == 0); // closed to begin with, seams included
    const MeshletMesh lod = buildMeshletLod(torus, optimizeIndexOrder(torus));
    CHECK(lod.levels >= 4);
    CHECK(lod.triangleCount(0) == torus.triangleCount());
    CHECK(lod.triangleCount(1) < torus.triangleCount() * 3 / 4); // each level about halves the triangles
    CHECK(badEdges(torus, selectedTriangles(lod, vec3(0.0f, 0.0f, 0.0f), 1000.0f, 0.0f)) == 0);
    for (const Meshlet &meshlet : lod.meshlets) {
        // Errors grow towards coarser levels, and a parent's sphere contains its children's.
        CHECK(meshlet.parentError >= meshlet.lodError);
        if (meshlet.parentError < kLodNoParent) {
            const float gap = glm::length(vec3(meshlet.parentSphere) - vec3(meshlet.lodSphere));
            CHECK(gap + meshlet.lodSphere.w <= meshlet.parentSphere.w * 1.0001f + 1e-5f);
        }
        CHECK((meshlet.level == 0) == (meshlet.lodError == 0.0f));
    }

    // From close up to far away: always closed, and fewer triangles the farther the camera.
    std::size_t previous = torus.triangleCount() + 1;
    for (float distance : {0.5f, 2.0f, 8.0f, 32.0f, 128.0f, 1024.0f}) {
        const vec3 camera = glm::normalize(vec3(0.3f, 0.8f, 0.5f)) * distance;
        const auto triangles = selectedTriangles(lod, camera, 540.0f, 1.0f);
        CHECK(badEdges(torus, triangles) == 0);
        CHECK(!triangles.empty() && triangles.size() <= previous);
        previous = triangles.size();
    }
    CHECK(previous < torus.triangleCount() / 50); // far away, a few dozen triangles do

    // Without a hierarchy every meshlet is level 0 and always drawn.
    const MeshletMesh flat = buildMeshlets(torus, optimizeIndexOrder(torus));
    for (const Meshlet &meshlet : flat.meshlets) {
        CHECK(meshlet.level == 0 && meshlet.parentError >= kLodNoParent);
        CHECK(lodSelected(meshlet, vec3(1000.0f), 540.0f, 1.0f));
    }
}

int main() {
    clusterLodIsCrackFree();
    loadsGltfFiles();
    texturesBuildMipChains();
    texturesSampleLikeTheGpu();
    meshesGenerateTangents();
    sphereIsClosedAndMatchesItsTangents();
    materialsPackForShaders();
    blendedItemsAreDrawnLastAndFarthestFirst();
    maskedTexelsAreCut();
    projectionMatchesVulkanClipSpace();
    cameraLooksAtItsTarget();
    transformScalesThenRotatesThenTranslates();
    meshesValidateAndVersion();
    objectIdsSurviveRemoval();
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
