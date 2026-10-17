#include <iostream>

#include "phox/Renderer.hpp"
#include "phox/Scene.hpp"

using namespace phox;

int main(int argc, char **argv) {
  std::unique_ptr<Renderer> r = Renderer::Create(RendererType::Vulkan);

  if (r) {
    try {
      r->initialize(false);
      Scene scene;
      scene.addObject(
          ObjectFactory::createTriangle("triangle", Material{"basic", "default"}));
      r->render(scene);
      r->cleanup();
    } catch (const std::exception &e) {
      std::cerr << "Renderer failed: " << e.what() << std::endl;
      return 1;
    }
  }

  return 0;
}
