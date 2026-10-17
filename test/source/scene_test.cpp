#include "phox/Camera.hpp"
#include "phox/Renderer.hpp"
#include "phox/Scene.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

int main() {
    phox::Scene scene;
    phox::VertexLayout layout = phox::VertexLayout::positionNormalTexcoord();
    phox::Material material{"test", "default", layout};
    auto triangle = phox::ObjectFactory::createTriangle("triangle", std::move(material));
    scene.addObject(std::move(triangle));

    phox::Camera camera;
    camera.setPosition({0.0f, 0.0f, 5.0f});
    camera.setFovY(60.0f);

    auto renderer = phox::Renderer::Create(phox::RendererType::Vulkan);
    renderer->initialize(false);
    renderer->render(scene);
    renderer->cleanup();

    const std::filesystem::path outputPath = "build/rendered_triangle.ppm";
    CHECK(std::filesystem::exists(outputPath));
    CHECK(std::filesystem::file_size(outputPath) > 0);

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
