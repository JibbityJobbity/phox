// Scene, camera and view extraction, the software rasterizer and mesh loading. No GPU needed.
#include "phox/MeshLoader.hpp"
#include "phox/RenderView.hpp"
#include "phox/Scene.hpp"
#include "phox/SoftwareRasterizer.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

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
    loadsObjFiles();
    std::cout << "render view tests passed" << std::endl;
    return 0;
}
