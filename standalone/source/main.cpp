#include "phox/Log.hpp"
#include "phox/MeshLoader.hpp"
#include "phox/Renderer.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/Window.hpp"

#include <cxxopts.hpp>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace phox;

namespace {

ToneMapOperator parseToneMap(const std::string &name) {
  if (name == "clamp") return ToneMapOperator::Clamp;
  if (name == "reinhard") return ToneMapOperator::Reinhard;
  if (name == "aces") return ToneMapOperator::Aces;
  throw std::invalid_argument("--tonemap expects clamp, reinhard or aces");
}

RendererType parseBackend(const std::string &name) {
  if (name == "vulkan") return RendererType::Vulkan;
  if (name == "metal") return RendererType::Metal;
  throw std::invalid_argument("--backend expects vulkan or metal");
}

Extent2D parseSize(const std::string &text) {
  unsigned width = 0, height = 0;
  if (std::sscanf(text.c_str(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0) {
    throw std::invalid_argument("--size expects WIDTHxHEIGHT, e.g. 1280x720");
  }
  return {width, height};
}

} // namespace

int main(int argc, char **argv) {
  cxxopts::Options options("PhoxSample", "Renders a Phox sample scene, headless or in a window.");
  // clang-format off
  options.add_options()
    ("scene", "sample scene: triangle or cubes", cxxopts::value<std::string>()->default_value("triangle"))
    ("mesh", "show a model file (OBJ) on a turntable instead of a sample scene", cxxopts::value<std::string>())
    ("window", "present to a window until it is closed")
    ("frames", "with --window: stop after this many frames", cxxopts::value<long>()->default_value("0"))
    ("time", "headless: the animation time to render, in seconds", cxxopts::value<float>()->default_value("0"))
    ("size", "headless: image size", cxxopts::value<std::string>()->default_value("640x480"))
    ("output", "headless: where to write the image (default build/rendered_<scene>.ppm)", cxxopts::value<std::string>())
    ("backend", "GPU backend: vulkan or metal (Apple platforms)", cxxopts::value<std::string>()->default_value("vulkan"))
    ("debug", "enable validation (the Vulkan validation layer, or Metal API validation) and debug logging")
    ("device", "use the GPU whose name contains this text", cxxopts::value<std::string>())
    ("tonemap", "tone mapping: clamp, reinhard or aces", cxxopts::value<std::string>()->default_value("clamp"))
    ("exposure", "scale scene colours by this before tone mapping", cxxopts::value<float>()->default_value("1"))
    ("dump-graph", "write the first frame's render graph (Graphviz DOT) to this file", cxxopts::value<std::string>())
    ("h,help", "show this help");
  // clang-format on

  try {
    const auto args = options.parse(argc, argv);
    if (args.count("help")) {
      std::cout << options.help() << std::endl;
      return 0;
    }

    SampleScene sample;
    if (args.count("mesh")) {
      const std::filesystem::path path = args["mesh"].as<std::string>();
      sample = makeMeshViewerScene(std::make_shared<Mesh>(loadMesh(path)), path.stem().string());
    } else {
      sample = makeSampleScene(args["scene"].as<std::string>());
    }

    sample.scene.toneMapping.op = parseToneMap(args["tonemap"].as<std::string>());
    sample.scene.toneMapping.exposure = args["exposure"].as<float>();

    const RendererType backend = parseBackend(args["backend"].as<std::string>());
    if (!Renderer::Available(backend)) {
      throw std::invalid_argument("this build has no " + args["backend"].as<std::string>() + " backend");
    }

    // Metal reads this once, when the process creates its first Metal device. A Metal window does that
    // before the renderer exists, so the renderer cannot turn validation on by itself.
    if (backend == RendererType::Metal && args.count("debug")) {
      setenv("MTL_DEBUG_LAYER", "1", 0);
    }

    // Declared before the renderer so it is destroyed after it.
    std::unique_ptr<Window> window;
    if (args.count("window")) {
      WindowConfig windowConfig;
      windowConfig.api = backend == RendererType::Metal ? GraphicsApi::Metal : GraphicsApi::Vulkan;
      window = Window::Create(windowConfig);
    }

    RendererConfig config;
    config.debug = args.count("debug") > 0;
    config.window = window.get();
    config.headlessExtent = parseSize(args["size"].as<std::string>());
    if (args.count("device")) {
      config.preferredDevice = args["device"].as<std::string>();
    }
    config.captureFrameGraph = args.count("dump-graph") > 0;
    // Writes the graph once, after the first frame that produced one.
    bool graphWritten = false;
    const auto dumpGraph = [&](const Renderer &r) {
      if (graphWritten || !config.captureFrameGraph || r.frameGraphDot().empty()) {
        return;
      }
      const std::filesystem::path path = args["dump-graph"].as<std::string>();
      if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
      }
      std::ofstream(path) << r.frameGraphDot();
      std::cout << "Wrote the frame graph to " << path.string() << std::endl;
      graphWritten = true;
    };
    std::unique_ptr<Renderer> renderer = Renderer::Create(backend);
    renderer->initialize(config);

    if (window) {
      const long maxFrames = args["frames"].as<long>();
      const auto start = std::chrono::steady_clock::now();
      long frames = 0;
      while (window->pollEvents()) {
        // Animate by elapsed time, not frame count, so the speed does not depend on the frame rate.
        const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        sample.animate(sample.scene, seconds);
        renderer->render(sample.scene);
        dumpGraph(*renderer);
        ++frames;
        if (maxFrames > 0 && frames >= maxFrames) {
          break;
        }
      }
      const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::cout << "Presented " << frames << " frame(s) in " << elapsed << " s" << std::endl;
    } else {
      sample.animate(sample.scene, args["time"].as<float>());
      renderer->render(sample.scene);
      dumpGraph(*renderer);
      const std::string output = args.count("output") ? args["output"].as<std::string>()
                                                      : "build/rendered_" + sample.name + ".ppm";
      writePpm(output, renderer->lastFrame());
      std::cout << "Wrote " << output << " (" << renderer->info().deviceName << ")" << std::endl;
    }

    const RendererInfo info = renderer->info();
    renderer->cleanup();
    if (info.validation && (info.validationErrors > 0 || info.validationWarnings > 0)) {
      std::cerr << "Validation reported " << info.validationErrors << " error(s) and " << info.validationWarnings
                << " warning(s)" << std::endl;
      return 3;
    }
  } catch (const cxxopts::OptionException &e) {
    std::cerr << e.what() << "\n\n" << options.help() << std::endl;
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "PhoxSample failed: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
