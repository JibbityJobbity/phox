#include "phox/Camera.hpp"
#include "phox/Renderer.hpp"
#include "phox/Scene.hpp"

#include <cstdlib>
#include <iostream>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

namespace {

bool containsColor(const phox::CpuImage &image, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (image.rgba[i] == r && image.rgba[i + 1] == g && image.rgba[i + 2] == b) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    phox::Scene scene;
    phox::VertexLayout layout = phox::VertexLayout::positionNormalTexcoord();
    phox::Material material{"test", "default", layout};
    auto triangle = phox::ObjectFactory::createTriangle("triangle", std::move(material));
    scene.addObject(std::move(triangle));

    phox::Camera camera;
    camera.setPosition({0.0f, 0.0f, 5.0f});
    camera.setFovY(60.0f);

    // Runs on the GPU when there is one, otherwise on the software fallback; both must produce a frame.
    auto renderer = phox::Renderer::Create(phox::RendererType::Vulkan);
    phox::RendererConfig config;
    config.headlessExtent = {160, 120};
    renderer->initialize(config);
    renderer->render(scene);
    const phox::CpuImage frame = renderer->lastFrame();
    std::cout << "rendered on " << renderer->info().deviceName << std::endl;
    renderer->cleanup();

    CHECK(frame.width == 160 && frame.height == 120);
    CHECK(frame.rgba.size() == 160u * 120u * 4u);
    CHECK(containsColor(frame, 255, 51, 102)); // the material albedo
    CHECK(!containsColor(frame, 0, 0, 0));     // nothing left uncleared

    CHECK(layout.hasAttribute(phox::VertexAttributeSemantic::Position));
    CHECK(layout.hasAttribute(phox::VertexAttributeSemantic::Normal));
    CHECK(layout.hasAttribute(phox::VertexAttributeSemantic::TexCoord0));
    CHECK(scene.objectCount() == 1);
    CHECK(scene.objects().size() == 1);
    CHECK(scene.objects()[0]->mesh().triangleCount() == 1);
    CHECK(scene.objects()[0]->material().name == "test");
    CHECK(camera.position()[2] == 5.0f);

    std::cout << "scene test passed" << std::endl;
    return 0;
}
