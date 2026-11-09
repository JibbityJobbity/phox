// Smoke test: builds the sample scenes and renders a frame headless, on the GPU if there is one and on
// the software fallback otherwise.
#include "phox/CpuImage.hpp"
#include "phox/Renderer.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/Scene.hpp"

#include <cstdlib>
#include <memory>
#include <string>
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

// A renderer keeps state across frames for each object (GPU-driven rendering: instance slots, visibility
// history and meshlet visibility bits). Objects leave, arrive and change mesh; the renderer that saw the
// old scene must then draw exactly what a fresh one draws.
void changingScenesDrawLikeFreshOnes(bool meshShaders) {
    phox::RendererConfig config;
    config.headlessExtent = {160, 120};
    config.meshShaders = meshShaders;
    auto seasoned = phox::Renderer::Create(phox::RendererType::Vulkan);
    seasoned->initialize(config);
    phox::SampleScene sample = phox::makeSampleScene("cubes");
    sample.animate(sample.scene, 0.3f);
    renderFrames(*seasoned, sample.scene, 4);

    // Two objects go, three arrive (reusing their slots, then one more), one changes mesh. The scene's
    // animation refers to the removed objects, so the pose stays as it is.
    phox::Scene &scene = sample.scene;
    CHECK(scene.removeObject(scene.find("red")) && scene.removeObject(scene.find("green")));
    auto torus = std::make_shared<phox::Mesh>(phox::Mesh::torus(0.4f, 0.15f, 32, 16));
    for (int i = 0; i < 3; ++i) {
        scene.addObject("new " + std::to_string(i), torus,
                        phox::Material{.name = "new", .albedo = {0.2f, 0.8f, 0.9f, 1.0f}, .doubleSided = false},
                        phox::Transform{{-1.5f + 1.5f * static_cast<float>(i), 0.3f, 0.6f}});
    }
    scene.at(scene.find("blue")).setMesh(std::make_shared<phox::Mesh>(phox::Mesh::sphere(0.6f)));
    const phox::CpuImage changed = renderFrames(*seasoned, scene, 3);

    auto fresh = phox::Renderer::Create(phox::RendererType::Vulkan);
    fresh->initialize(config);
    const phox::CpuImage expected = renderFrames(*fresh, scene, 3);
    const phox::ImageDiff diff = phox::compareImages(changed, expected, 2);
    std::cout << "changed scene (" << (meshShaders && seasoned->info().meshShaders ? "mesh shaders" : "vertex shaders")
              << "): " << diff.pixelsOverTolerance << " pixel(s) unlike a fresh renderer's" << std::endl;
    CHECK(!diff.sizeMismatch && diff.pixelsOverTolerance == 0);
    seasoned->cleanup();
    fresh->cleanup();
}

} // namespace

int main() {
    CHECK(phox::sampleSceneNames().size() == 6);
    for (const std::string &name : phox::sampleSceneNames()) {
        CHECK(phox::makeSampleScene(name).scene.objectCount() > 0);
    }
    CHECK(phox::makeSampleScene("field").scene.objectCount() == 24 * 24);
    phox::SampleScene cubes = phox::makeSampleScene("cubes");
    CHECK(cubes.scene.objectCount() == 6);
    const phox::ObjectId red = cubes.scene.find("red");
    CHECK(cubes.scene.contains(red));
    CHECK(!cubes.scene.find("nothing").valid());
    const phox::quat before = cubes.scene.at(red).transform().rotation;
    cubes.animate(cubes.scene, 1.0f);
    CHECK(cubes.scene.at(red).transform().rotation != before);

    bool threw = false;
    try {
        phox::makeSampleScene("nope");
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);

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
