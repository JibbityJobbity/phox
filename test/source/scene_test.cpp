// Smoke test for the legacy renderer (removed together with it). It also exercises the scene types.

#include "Check.hpp"

#include "phox/Renderer.hpp"
#include "phox/scene/Camera.hpp"
#include "phox/scene/Scene.hpp"

#include <filesystem>
#include <iostream>

int main() {
    phox::scene::Scene scene;
    phox::scene::Material material{"test", "default"};
    auto triangle = phox::scene::ObjectFactory::createTriangle("triangle", std::move(material));
    scene.addObject(std::move(triangle));

    phox::scene::Camera camera;
    camera.setPosition({0.0f, 0.0f, 5.0f});
    camera.setFovY(60.0f);

    auto renderer = phox::Renderer::Create(phox::RendererType::Vulkan);
    renderer->initialize(false);
    renderer->render(scene);
    renderer->cleanup();

    const std::filesystem::path outputPath = "build/rendered_triangle.ppm";
    CHECK(std::filesystem::exists(outputPath));
    CHECK(std::filesystem::file_size(outputPath) > 0);

    CHECK(scene.objectCount() == 1);
    CHECK(scene.objects().size() == 1);
    CHECK(scene.objects()[0]->mesh().triangleCount() == 1);
    CHECK(scene.objects()[0]->material().name == "test");
    CHECK(camera.position()[2] == 5.0f);

    std::cout << "scene test passed" << std::endl;
    return 0;
}
