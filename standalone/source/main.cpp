#include <cstdlib>
#include <cstring>
#include <iostream>

#include "phox/Renderer.hpp"
#include "phox/Scene.hpp"
#include "phox/Window.hpp"

using namespace phox;

// Usage: PhoxSample [--window [--frames N]]
//   (default)   render one frame headless to build/rendered_triangle.ppm
//   --window    open a window and present until it is closed (or N frames, if given)
int main(int argc, char **argv) {
  bool windowed = false;
  long maxFrames = 0; // 0 = until the window is closed
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--window") == 0) {
      windowed = true;
    } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      maxFrames = std::strtol(argv[++i], nullptr, 10);
    } else {
      std::cerr << "usage: " << argv[0] << " [--window [--frames N]]\n";
      return 2;
    }
  }

  try {
    // Declared before the renderer so it is destroyed after it.
    std::unique_ptr<Window> window;
    if (windowed) {
      window = Window::Create();
    }

    std::unique_ptr<Renderer> r = Renderer::Create(RendererType::Vulkan);
    r->initialize(false, window.get());

    Scene scene;
    scene.addObject(
        ObjectFactory::createTriangle("triangle", Material{"basic", "default"}));

    if (window) {
      long frames = 0;
      while (window->pollEvents()) {
        r->render(scene);
        if (maxFrames > 0 && ++frames >= maxFrames) {
          break;
        }
      }
      std::cout << "Presented " << frames << " frame(s)" << std::endl;
    } else {
      r->render(scene);
    }
    r->cleanup();
  } catch (const std::exception &e) {
    std::cerr << "Renderer failed: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
