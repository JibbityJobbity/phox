// Smoke test: builds the sample scenes and renders a frame headless, on the GPU if there is one and on
// the software fallback otherwise.
#include "phox/CpuImage.hpp"
#include "phox/Renderer.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/Scene.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <iostream>

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                                     \
        }                                                                                     \
    } while (0)

namespace {

std::size_t pixelsUnlike(const phox::CpuImage &image, std::array<std::uint8_t, 4> background) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (image.rgba[i] != background[0] || image.rgba[i + 1] != background[1] || image.rgba[i + 2] != background[2]) {
            ++count;
        }
    }
    return count;
}

// Renders @p scene for @p frames frames and returns the last.
phox::CpuImage renderFrames(phox::Renderer &renderer, const phox::Scene &scene, int frames) {
    for (int i = 0; i < frames; ++i) renderer.render(scene);
    return renderer.lastFrame();
}

// Checks that @p seasoned, which has drawn earlier versions of @p scene, now draws what a fresh renderer does.
void expectLikeFresh(phox::Renderer &seasoned, const phox::Scene &scene, const phox::RendererConfig &config,
                     const char *what) {
    const phox::CpuImage changed = renderFrames(seasoned, scene, 3);
    auto fresh = phox::Renderer::Create(phox::RendererType::Vulkan);
    fresh->initialize(config);
    const phox::CpuImage expected = renderFrames(*fresh, scene, 3);
    const phox::ImageDiff diff = phox::compareImages(changed, expected, 2);
    std::cout << what << " (" << (config.meshShaders && seasoned.info().meshShaders ? "mesh shaders" : "vertex shaders")
              << "): " << diff.pixelsOverTolerance << " pixel(s) unlike a fresh renderer's" << std::endl;
    CHECK(!diff.sizeMismatch && diff.pixelsOverTolerance == 0);
    fresh->cleanup();
}

// The objects of the "cubes" sample scene, which adds them in this order. Read from a scene that has not
// had objects removed.
struct CubesObjects {
    phox::ObjectId floor, red, green, blue, yellow, purple;
};
CubesObjects cubesObjects(const phox::Scene &scene) {
    const auto ids = scene.objectIds();
    CHECK(ids.size() == 6);
    return {ids[0], ids[1], ids[2], ids[3], ids[4], ids[5]};
}

// A renderer keeps state across frames for each object (GPU-driven rendering: the objects themselves,
// visibility history and meshlet visibility bits), and updates only what changed. Objects leave, arrive,
// change mesh, material and transform, and meshes change geometry; the renderer that saw the old scene
// must then draw exactly what a fresh one draws.
void changingScenesDrawLikeFreshOnes(bool meshShaders) {
    phox::RendererConfig config;
    config.headlessExtent = {160, 120};
    config.meshShaders = meshShaders;
    auto seasoned = phox::Renderer::Create(phox::RendererType::Vulkan);
    seasoned->initialize(config);
    phox::SampleScene sample = phox::makeSampleScene(phox::SampleSceneId::Cubes);
    sample.animate(sample.scene, 0.3f);
    renderFrames(*seasoned, sample.scene, 4);
    const CubesObjects cubes = cubesObjects(sample.scene);

    // Two objects go, three arrive (reusing their slots, then one more), one changes mesh. The scene's
    // animation refers to the removed objects, so the pose stays as it is.
    phox::Scene &scene = sample.scene;
    CHECK(scene.removeObject(cubes.red) && scene.removeObject(cubes.green));
    auto torus = std::make_shared<phox::Mesh>(phox::Mesh::torus(0.4f, 0.15f, 32, 16));
    const phox::Material cyan{.albedo = {0.2f, 0.8f, 0.9f, 1.0f}, .doubleSided = false};
    const auto [arrival, second, third] = scene.addObjects({
        {.mesh = torus, .material = cyan, .transform = {.position = {-1.5f, 0.3f, 0.6f}}},
        {.mesh = torus, .material = cyan, .transform = {.position = {0.0f, 0.3f, 0.6f}}},
        {.mesh = torus, .material = cyan, .transform = {.position = {1.5f, 0.3f, 0.6f}}},
    });
    CHECK(scene.contains(second) && scene.contains(third));
    scene.at(cubes.blue).setMesh(std::make_shared<phox::Mesh>(phox::Mesh::sphere(0.6f)));
    expectLikeFresh(*seasoned, scene, config, "changed objects");

    // In place: a material recoloured, one made translucent, one moved, and the torus the new objects
    // share given other geometry (more meshlets).
    scene.at(cubes.yellow).material().albedo = {0.1f, 0.9f, 0.2f, 1.0f};
    phox::Material &purple = scene.at(cubes.purple).material();
    purple.alphaMode = phox::AlphaMode::Blend;
    purple.albedo.a = 0.5f;
    scene.at(arrival).transform().position.y += 0.5f;
    const phox::Mesh sphere = phox::Mesh::sphere(0.5f, 64, 32);
    torus->setGeometry(sphere.vertices(), sphere.indices());
    expectLikeFresh(*seasoned, scene, config, "edited objects");

    // Back to opaque, and the floor loses its mesh.
    scene.at(cubes.purple).material().alphaMode = phox::AlphaMode::Opaque;
    scene.at(cubes.floor).setMesh(nullptr);
    expectLikeFresh(*seasoned, scene, config, "edited again");

    // A crowd arrives: more meshlet visibility words than the renderer has, so it starts them over and
    // rewrites every object mid-session.
    auto pebble = std::make_shared<phox::Mesh>(phox::Mesh::cube(0.02f));
    for (int i = 0; i < 20000; ++i) {
        const float x = static_cast<float>(i % 200) * 0.03f - 3.0f;
        const float y = static_cast<float>(i / 200) * 0.03f - 1.0f;
        scene.addObject({.mesh = pebble,
                         .material = {.albedo = {0.9f, 0.9f, 0.9f, 1.0f}},
                         .transform = {.position = {x, y, -2.0f}}});
    }
    expectLikeFresh(*seasoned, scene, config, "crowd arrived");
    seasoned->cleanup();
}

// Scene::changes: what a renderer that keeps copies of the objects reads each frame.
void changeLogTracksObjects() {
    phox::Scene scene;
    auto cube = std::make_shared<phox::Mesh>(phox::Mesh::cube());
    const auto [a, b] = scene.addObjects({{.mesh = cube}, {.mesh = cube}});
    phox::Scene::ChangeCursor cursor;
    CHECK(!scene.changes(cursor)); // a new reader looks at everything
    CHECK(scene.changes(cursor)->empty());

    // Changed twice, logged once; reading the const side logs nothing.
    scene.at(a).transform().position.x = 1.0f;
    scene.at(a).material().roughness = 0.2f;
    CHECK(std::as_const(scene).at(b).transform().position.x == 0.0f);
    auto changes = scene.changes(cursor);
    CHECK(changes && changes->size() == 1 && (*changes)[0] == a.index);
    // Once read, a change is logged again.
    scene.at(a).setMesh(nullptr);
    changes = scene.changes(cursor);
    CHECK(changes && changes->size() == 1 && (*changes)[0] == a.index);

    // Removal moves b into a's place in the object array, but b keeps its index; both are logged.
    CHECK(scene.removeObject(a));
    const phox::ObjectId c = scene.addObject({.mesh = cube});
    changes = scene.changes(cursor);
    CHECK(changes && changes->size() == 2 && (*changes)[0] == a.index && (*changes)[1] == c.index);
    CHECK(c.index == a.index && scene.objectAt(c.index) == &scene.at(c));
    scene.at(b).transform().position.y = 2.0f;
    changes = scene.changes(cursor);
    CHECK(changes && changes->size() == 1 && (*changes)[0] == b.index);
    CHECK(scene.objectAt(99) == nullptr);

    // A moved scene keeps its log, and its objects still log to it.
    phox::Scene moved = std::move(scene);
    moved.at(b).transform().position.y = 3.0f;
    changes = moved.changes(cursor);
    CHECK(changes && changes->size() == 1);
    // A copy has a log of its own: its objects log there, and a cursor from the original reads it whole.
    phox::Scene copy = moved;
    copy.at(b).transform().position.y = 4.0f;
    CHECK(moved.changes(cursor)->empty());
    phox::Scene::ChangeCursor copyCursor = cursor;
    CHECK(!copy.changes(copyCursor));

    // Unread, the log starts over once it outgrows the scene; a reader that fell behind looks at everything.
    phox::Scene::ChangeCursor behind;
    copy.changes(behind);
    for (int i = 0; i < 3000; ++i) {
        copy.at(b).transform().position.z = static_cast<float>(i);
        copy.changes(copyCursor); // keeps reading, so every change is logged
    }
    CHECK(!copy.changes(behind));
    CHECK(copy.changes(copyCursor)->empty());
}

} // namespace

int main() {
    changeLogTracksObjects();
    CHECK(phox::kSampleScenes.size() == 6);
    for (phox::SampleSceneId id : phox::kSampleScenes) {
        const phox::SampleScene sample = phox::makeSampleScene(id);
        CHECK(sample.scene.objectCount() > 0);
        CHECK(sample.name == phox::sampleSceneName(id) && phox::parseSampleScene(sample.name) == id);
    }
    CHECK(!phox::parseSampleScene("nope"));
    CHECK(phox::makeSampleScene(phox::SampleSceneId::Field).scene.objectCount() == 24 * 24);
    phox::SampleScene cubes = phox::makeSampleScene(phox::SampleSceneId::Cubes);
    CHECK(cubes.scene.objectCount() == 6);
    const phox::ObjectId red = cubesObjects(cubes.scene).red;
    CHECK(cubes.scene.contains(red));
    const phox::quat before = cubes.scene.at(red).transform().rotation;
    cubes.animate(cubes.scene, 1.0f);
    CHECK(cubes.scene.at(red).transform().rotation != before);

    auto renderer = phox::Renderer::Create(phox::RendererType::Vulkan);
    phox::RendererConfig config;
    config.headlessExtent = {160, 120};
    renderer->initialize(config);
    renderer->render(cubes.scene);
    const phox::CpuImage frame = renderer->lastFrame();
    const phox::RendererInfo info = renderer->info();
    std::cout << "rendered on " << info.deviceName << (info.meshShaders ? " with mesh shaders" : "") << std::endl;
    // A headless frame is finished when render() returns, so its GPU timings are already in.
    if (info.backend == phox::RendererBackend::Vulkan && !renderer->gpuTimings().empty()) {
        bool forward = false;
        for (const phox::PassTiming &timing : renderer->gpuTimings()) {
            forward = forward || timing.pass.rfind("forward", 0) == 0;
            std::cout << "  " << timing.pass << ": " << timing.milliseconds << " ms" << std::endl;
            CHECK(timing.milliseconds >= 0.0);
        }
        CHECK(forward);
    }
    renderer->cleanup();

    CHECK(frame.width == 160 && frame.height == 120);
    CHECK(frame.rgba.size() == 160u * 120u * 4u);
    // The background is (0.1, 0.2, 0.3); the cubes and floor must cover a good part of the frame.
    CHECK(pixelsUnlike(frame, {26, 51, 76, 255}) > frame.width * frame.height / 4);

    changingScenesDrawLikeFreshOnes(true);
    changingScenesDrawLikeFreshOnes(false);

    std::cout << "scene test passed" << std::endl;
    return 0;
}
