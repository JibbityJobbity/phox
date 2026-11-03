#include "phox/Log.hpp"
#include "phox/MeshLoader.hpp"
#include "phox/Renderer.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/Window.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace phox;

namespace {

ToneMapOperator parseToneMap(const std::string &name) {
  if (name == "clamp") return ToneMapOperator::Clamp;
  if (name == "reinhard") return ToneMapOperator::Reinhard;
  if (name == "aces") return ToneMapOperator::Aces;
  throw std::invalid_argument("--tonemap expects clamp, reinhard or aces");
}

Extent2D parseSize(const std::string &text) {
  unsigned width = 0, height = 0;
  if (std::sscanf(text.c_str(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0) {
    throw std::invalid_argument("--size expects WIDTHxHEIGHT, e.g. 1280x720");
  }
  return {width, height};
}

// Sums GPU pass times over frames, to print their averages.
class TimingAverages {
public:
  void add(const std::vector<PassTiming> &timings) {
    if (timings.empty()) {
      return;
    }
    for (const PassTiming &timing : timings) {
      auto it = std::find_if(m_totals.begin(), m_totals.end(), [&](const auto &t) { return t.first == timing.pass; });
      if (it == m_totals.end()) {
        m_totals.emplace_back(timing.pass, 0.0);
        it = std::prev(m_totals.end());
      }
      it->second += timing.milliseconds;
    }
    ++m_frames;
  }

  void print() const {
    if (m_frames == 0) {
      return;
    }
    double total = 0.0;
    std::cout << "GPU time per frame, average of " << m_frames << " frame(s):";
    for (const auto &[pass, milliseconds] : m_totals) {
      std::cout << " " << pass << " " << milliseconds / m_frames << " ms,";
      total += milliseconds;
    }
    std::cout << " total " << total / m_frames << " ms" << std::endl;
  }

private:
  std::vector<std::pair<std::string, double>> m_totals;
  long m_frames = 0;
};

} // namespace

int main(int argc, char **argv) {
  cxxopts::Options options("PhoxSample", "Renders a Phox sample scene, headless or in a window.");
  // clang-format off
  options.add_options()
    ("scene", "sample scene: triangle or cubes", cxxopts::value<std::string>()->default_value("triangle"))
    ("mesh", "show a model file (OBJ) on a turntable instead of a sample scene", cxxopts::value<std::string>())
    ("window", "present to a window until it is closed")
    ("frames", "with --window: stop after this many frames; headless: render this many frames (one image) and "
               "report average GPU times", cxxopts::value<long>()->default_value("0"))
    ("time", "headless: the animation time to render, in seconds", cxxopts::value<float>()->default_value("0"))
    ("animate", "headless with --frames: advance the animation by 1/60 s per frame instead of repeating one pose")
    ("size", "headless: image size", cxxopts::value<std::string>()->default_value("640x480"))
    ("output", "headless: where to write the image (default build/rendered_<scene>.ppm)", cxxopts::value<std::string>())
    ("debug", "enable the Vulkan validation layer and debug logging")
    ("device", "use the GPU whose name contains this text", cxxopts::value<std::string>())
    ("tonemap", "tone mapping: clamp, reinhard or aces", cxxopts::value<std::string>()->default_value("clamp"))
    ("exposure", "scale scene colours by this before tone mapping", cxxopts::value<float>()->default_value("1"))
    ("mesh-shaders", "draw with task and mesh shaders where available: on, off, noocclusion (frustum and cone "
                     "culling only) or nocull (no meshlet culling)", cxxopts::value<std::string>()->default_value("on"))
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

    // Declared before the renderer so it is destroyed after it.
    std::unique_ptr<Window> window;
    if (args.count("window")) {
      window = Window::Create();
    }

    RendererConfig config;
    config.debug = args.count("debug") > 0;
    config.window = window.get();
    config.headlessExtent = parseSize(args["size"].as<std::string>());
    if (args.count("device")) {
      config.preferredDevice = args["device"].as<std::string>();
    }
    config.captureFrameGraph = args.count("dump-graph") > 0;
    const std::string meshShaders = args["mesh-shaders"].as<std::string>();
    if (meshShaders != "on" && meshShaders != "off" && meshShaders != "noocclusion" && meshShaders != "nocull") {
      throw std::invalid_argument("--mesh-shaders expects on, off, noocclusion or nocull");
    }
    config.meshShaders = meshShaders != "off";
    config.meshletCulling = meshShaders != "nocull";
    config.occlusionCulling = meshShaders == "on";
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
    std::unique_ptr<Renderer> renderer = Renderer::Create(RendererType::Vulkan);
    renderer->initialize(config);
    const RendererInfo startInfo = renderer->info();
    std::cout << "Rendering on " << startInfo.deviceName << " with "
              << (startInfo.backend == RendererBackend::Software ? "the software rasterizer"
                  : !startInfo.meshShaders                       ? "vertex shaders"
                  : startInfo.occlusionCulling                   ? "mesh shaders, frustum, cone and occlusion culling"
                  : config.meshletCulling                        ? "mesh shaders, frustum and cone culling"
                                                                 : "mesh shaders, no meshlet culling")
              << std::endl;
    // The first frames upload meshes and create pooled images, so they are left out of the averages.
    constexpr long kWarmupFrames = 3;
    TimingAverages timings;

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
        if (frames >= kWarmupFrames) {
          timings.add(renderer->gpuTimings());
        }
        if (++frames >= maxFrames && maxFrames > 0) {
          break;
        }
      }
      const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::cout << "Presented " << frames << " frame(s) in " << elapsed << " s" << std::endl;
      timings.print();
    } else {
      // Without --animate every frame shows the same pose, so the GPU does the same work each time.
      const float time = args["time"].as<float>();
      const bool animate = args.count("animate") > 0;
      sample.animate(sample.scene, time);
      const long frames = std::max(1L, args["frames"].as<long>());
      for (long i = 0; i < frames; ++i) {
        if (animate) {
          sample.animate(sample.scene, time + static_cast<float>(i) / 60.0f);
        }
        renderer->render(sample.scene);
        if (i >= kWarmupFrames || frames <= kWarmupFrames) {
          timings.add(renderer->gpuTimings());
        }
      }
      dumpGraph(*renderer);
      if (frames > 1) {
        timings.print();
      }
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
