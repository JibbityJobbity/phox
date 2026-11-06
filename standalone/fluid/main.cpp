// PhoxFluidSample: a 2D fluid simulated on the GPU, on the async compute queue where there is one, and
// drawn as a surface the GPU builds every frame. The solver is swappable: --solver stable (Navier-Stokes,
// Stam's stable fluids), lbm (lattice Boltzmann, D2Q9) or sph (smoothed particle hydrodynamics). Emitters
// circle the tank and stir it; in a window, dragging with the mouse pushes the fluid too.
#include "fluid/FluidFeature.hpp"
#include "fluid/LatticeBoltzmann.hpp"
#include "fluid/SmoothedParticles.hpp"
#include "fluid/StableFluids.hpp"

#include "phox/CpuImage.hpp"
#include "phox/Log.hpp"
#include "phox/Window.hpp"
#include "phox/vulkan/Renderer.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

using namespace phox;

namespace {

Extent2D parseSize(const std::string &text) {
  unsigned width = 0, height = 0;
  if (std::sscanf(text.c_str(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0) {
    throw std::invalid_argument("--size expects WIDTHxHEIGHT, e.g. 1280x720");
  }
  return {width, height};
}

bool parseOnOff(const cxxopts::ParseResult &args, const char *name) {
  const std::string value = args[name].as<std::string>();
  if (value != "on" && value != "off") {
    throw std::invalid_argument(std::string("--") + name + " expects on or off");
  }
  return value == "on";
}

// Emitters on a circle, turning slowly, each pushing along the circle and adding dye.
fluid::FluidInputs emitters(float time, float dt, int count) {
  fluid::FluidInputs inputs;
  inputs.dt = dt;
  for (int i = 0; i < count; ++i) {
    const float angle = 0.5f * time + static_cast<float>(i) * 2.0943951f;
    const vec2 around{std::cos(angle), std::sin(angle)};
    const vec2 along{-around.y, around.x};
    inputs.splats.push_back({vec2(0.5f) + 0.3f * around, 6.0f * along - 1.5f * around, 0.04f, 2.0f});
  }
  return inputs;
}

Scene makeScene(float tankSize) {
  Scene scene;
  Material floor;
  floor.name = "tank floor";
  floor.albedo = {0.35f, 0.33f, 0.3f, 1.0f};
  floor.roughness = 0.9f;
  scene.addObject("floor", std::make_shared<Mesh>(Mesh::plane(tankSize * 1.6f)), floor, Transform{{0.0f, -0.4f, 0.0f}});
  scene.camera().position = {0.0f, 3.6f, 4.4f};
  scene.camera().lookAt({0.0f, -0.2f, 0.0f});
  scene.light().direction = {-0.4f, -1.0f, -0.5f};
  return scene;
}

// GPU time per pass, averaged over frames; passes of the same name (the Jacobi iterations) are summed.
class Timings {
public:
  void add(const std::vector<PassTiming> &timings) {
    if (timings.empty()) return;
    for (const PassTiming &timing : timings) m_totals[timing.pass] += timing.milliseconds;
    ++m_frames;
  }
  void print() const {
    if (m_frames == 0) return;
    std::cout << "GPU time per frame, average of " << m_frames << " frame(s) (async passes overlap the others):\n";
    for (const auto &[pass, milliseconds] : m_totals) {
      std::cout << "  " << pass << ": " << milliseconds / m_frames << " ms\n";
    }
  }

private:
  std::map<std::string, double> m_totals;
  long m_frames = 0;
};

} // namespace

int main(int argc, char **argv) {
  cxxopts::Options options("PhoxFluidSample", "A 2D fluid on the GPU, drawn as a surface.");
  // clang-format off
  options.add_options()
    ("solver", "stable (Navier-Stokes, stable fluids), lbm (lattice Boltzmann) or sph (smoothed particle hydrodynamics)",
     cxxopts::value<std::string>()->default_value("stable"))
    ("resolution", "grid cells a side", cxxopts::value<unsigned>()->default_value("192"))
    ("steps", "solver steps per frame (default: 1 for stable, 6 for lbm, 8 for sph)", cxxopts::value<unsigned>())
    ("particles", "sph: particles a side", cxxopts::value<unsigned>()->default_value("128"))
    ("iterations", "stable: Jacobi iterations per step", cxxopts::value<unsigned>()->default_value("40"))
    ("pipelined", "draw last frame's surface while this frame simulates (on or off)", cxxopts::value<std::string>()->default_value("on"))
    ("async-compute", "simulate on a separate compute queue where the GPU has one (on or off)", cxxopts::value<std::string>()->default_value("on"))
    ("window", "show it in a window")
    ("frames", "frames to run (headless: then write the image; window: 0 runs until closed)", cxxopts::value<long>()->default_value("240"))
    ("size", "image size, headless", cxxopts::value<std::string>()->default_value("1280x720"))
    ("output", "headless: where to write the image", cxxopts::value<std::string>()->default_value("build/fluid.ppm"))
    ("debug", "enable the validation layer and debug logging")
    ("device", "use the GPU whose name contains this text", cxxopts::value<std::string>())
    ("h,help", "show this help");
  // clang-format on

  try {
    const auto args = options.parse(argc, argv);
    if (args.count("help")) {
      std::cout << options.help() << std::endl;
      return 0;
    }
    const std::string solverName = args["solver"].as<std::string>();
    std::unique_ptr<fluid::FluidSolver> solver;
    fluid::FluidFeature::Settings settings;
    settings.resolution = args["resolution"].as<unsigned>();
    if (solverName == "stable") {
      fluid::StableFluids::Settings stable;
      stable.iterations = args["iterations"].as<unsigned>();
      solver = std::make_unique<fluid::StableFluids>(stable);
      settings.steps = 1;
      settings.heightScale = 0.25f; // dye
    } else if (solverName == "lbm") {
      solver = std::make_unique<fluid::LatticeBoltzmann>();
      settings.steps = 6;
      settings.heightScale = 20.0f; // density varies by about a percent
    } else if (solverName == "sph") {
      fluid::SmoothedParticles::Settings sph;
      sph.side = args["particles"].as<unsigned>();
      solver = std::make_unique<fluid::SmoothedParticles>(sph);
      settings.steps = 8;
      settings.heightScale = 6.0f; // density varies by a few percent
    } else {
      throw std::invalid_argument("--solver expects stable, lbm or sph");
    }
    if (args.count("steps")) settings.steps = args["steps"].as<unsigned>();
    settings.pipelined = parseOnOff(args, "pipelined");
    settings.size = 4.0f;
    settings.material.name = "fluid";
    settings.material.albedo = {0.05f, 0.3f, 0.6f, 1.0f};
    settings.material.roughness = 0.2f;
    auto feature = std::make_shared<fluid::FluidFeature>(std::move(solver), settings);

    std::unique_ptr<Window> window;
    if (args.count("window")) {
      window = Window::Create(WindowConfig{});
    }
    RendererConfig config;
    config.debug = args.count("debug") > 0;
    config.window = window.get();
    config.headlessExtent = parseSize(args["size"].as<std::string>());
    config.asyncCompute = parseOnOff(args, "async-compute");
    if (args.count("device")) config.preferredDevice = args["device"].as<std::string>();

    vk::Renderer renderer;
    renderer.addFeature(feature);
    renderer.initialize(config);
    const RendererInfo info = renderer.info();
    if (info.backend != RendererBackend::Vulkan) {
      throw std::runtime_error("the fluid sample needs a Vulkan device");
    }
    std::cout << "Simulating " << feature->solver().name() << " on a " << settings.resolution << "x"
              << settings.resolution << " grid, " << settings.steps << " step(s) per frame, on " << info.deviceName
              << (info.asyncCompute ? " (async compute queue" : " (graphics queue")
              << (settings.pipelined ? ", pipelined)" : ")") << std::endl;

    Scene scene = makeScene(settings.size);
    constexpr float kDt = 1.0f / 60.0f;
    constexpr long kWarmupFrames = 3;
    Timings timings;
    const long maxFrames = args["frames"].as<long>();
    const auto start = std::chrono::steady_clock::now();
    long frame = 0;
    // A fixed step per frame, so the fluid is the same whatever the frame rate.
    std::optional<vec2> lastPointer;
    while (window ? window->pollEvents() : frame < std::max(1L, maxFrames)) {
      const float dt = kDt / static_cast<float>(settings.steps);
      fluid::FluidInputs inputs = emitters(static_cast<float>(frame) * kDt, dt, 3);
      if (window) {
        // Dragging pushes the fluid along the drag, at the drag's speed, in place of the third emitter.
        int width = 0, height = 0;
        window->framebufferSize(width, height);
        const PointerState pointer = window->pointer();
        const std::optional<vec2> onTank =
            height > 0 && pointer.inside
                ? fluid::pointOnSurface(scene.camera(), pointer.x, pointer.y,
                                        static_cast<float>(width) / static_cast<float>(height), settings.model, settings.size)
                : std::nullopt;
        if (pointer.pressed && onTank && lastPointer) {
          const vec2 dragVelocity = (*onTank - *lastPointer) / kDt;
          inputs.splats.back() = {*onTank, 40.0f * dragVelocity, 0.04f, 4.0f};
        }
        lastPointer = pointer.pressed ? onTank : std::nullopt;
      }
      feature->setInputs(std::move(inputs));
      renderer.render(scene);
      if (frame >= kWarmupFrames) timings.add(renderer.gpuTimings());
      ++frame;
      if (window && maxFrames > 0 && frame >= maxFrames) break;
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << frame << " frame(s) in " << elapsed << " s" << std::endl;
    timings.print();
    if (!window) {
      const std::filesystem::path output = args["output"].as<std::string>();
      if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
      writePpm(output, renderer.lastFrame());
      std::cout << "Wrote " << output.string() << std::endl;
    }
    const RendererInfo end = renderer.info();
    renderer.cleanup();
    if (config.debug && end.validationErrors + end.validationWarnings > 0) {
      std::cerr << end.validationErrors << " validation error(s), " << end.validationWarnings << " warning(s)" << std::endl;
      return 3;
    }
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
