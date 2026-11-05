// Smoke test: builds the sample scenes and renders a frame headless, on the GPU if there is one and on
// the software fallback otherwise.
#include "phox/Renderer.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/Scene.hpp"

#include <cstdlib>
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

} // namespace

int main() {
    CHECK(phox::sampleSceneNames().size() == 2);
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
    std::cout << "rendered on " << renderer->info().deviceName << std::endl;
    renderer->cleanup();

    CHECK(frame.width == 160 && frame.height == 120);
    CHECK(frame.rgba.size() == 160u * 120u * 4u);
    // The background is (0.1, 0.2, 0.3); the cubes and floor must cover a good part of the frame.
    CHECK(pixelsUnlike(frame, {26, 51, 76, 255}) > frame.width * frame.height / 4);

    std::cout << "scene test passed" << std::endl;
    return 0;
}
