// The fluid sample's solvers (standalone/fluid), run on the GPU through vk::Renderer and read back:
// - at rest without forces, both stay at rest;
// - stable fluids' projection leaves the velocity nearly divergence free, far more so than without it;
// - lattice Boltzmann conserves mass while it is stirred;
// - both stay finite and move when stirred, within the lattice's speed limit for LBM;
// - the async compute queue and the graphics queue compute the same fields, bit for bit;
// - pipelined frames show the previous frame's surface: frame n + 1 pipelined looks like frame n without.
//
// Environment: PHOX_VALIDATION=1 (fail on any validation message), PHOX_REQUIRE_GPU=1 (fail instead of
// skipping without a Vulkan device), PHOX_DEVICE=<name>.
#include "fluid/FluidFeature.hpp"
#include "fluid/LatticeBoltzmann.hpp"
#include "fluid/StableFluids.hpp"

#include "phox/CpuImage.hpp"
#include "phox/Scene.hpp"
#include "phox/vulkan/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr int kSkip = 77;
constexpr std::uint32_t kResolution = 64;
const phox::Extent2D kExtent{256, 192};

bool envFlag(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

int failures = 0;
void check(bool ok, const std::string &what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

fluid::FluidInputs stir(int frame) {
    fluid::FluidInputs inputs;
    const float angle = 0.05f * static_cast<float>(frame);
    const phox::vec2 around{std::cos(angle), std::sin(angle)};
    inputs.splats.push_back({phox::vec2(0.5f) + 0.25f * around, phox::vec2(-around.y, around.x) * 8.0f, 0.06f, 2.0f});
    inputs.splats.push_back({phox::vec2(0.5f) - 0.25f * around, phox::vec2(around.y, -around.x) * 8.0f, 0.06f, 2.0f});
    return inputs;
}

// Nothing but the fluid (which also covers a frame with no scene items at all).
phox::Scene makeScene() {
    phox::Scene scene;
    scene.camera().position = {0.0f, 3.0f, 3.5f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    return scene;
}

struct Result {
    fluid::FluidFeature::Readback fields;
    phox::CpuImage image;
    phox::RendererInfo info;
};

using MakeSolver = std::function<std::unique_ptr<fluid::FluidSolver>()>;

// Runs @p frames frames, stirred by stir() while @p stirred, and reads the last one back.
Result run(const MakeSolver &makeSolver, int frames, bool stirred, bool asyncCompute = true, bool pipelined = true,
           std::uint32_t steps = 1) {
    fluid::FluidFeature::Settings settings;
    settings.resolution = kResolution;
    settings.steps = steps;
    settings.pipelined = pipelined;
    settings.heightScale = 0.3f;
    auto feature = std::make_shared<fluid::FluidFeature>(makeSolver(), settings);
    phox::vk::Renderer renderer;
    phox::RendererConfig config;
    config.debug = envFlag("PHOX_VALIDATION");
    config.headlessExtent = kExtent;
    config.asyncCompute = asyncCompute;
    renderer.addFeature(feature);
    renderer.initialize(config);
    Result result;
    result.info = renderer.info();
    if (result.info.backend != phox::RendererBackend::Vulkan) {
        return result;
    }
    const phox::Scene scene = makeScene();
    for (int f = 0; f < frames; ++f) {
        feature->setInputs(stirred ? stir(f) : fluid::FluidInputs{});
        if (f == frames - 1) feature->requestReadback();
        renderer.render(scene);
    }
    result.fields = feature->readback().value();
    result.image = renderer.lastFrame();
    result.info = renderer.info();
    renderer.cleanup();
    if (result.info.validationErrors + result.info.validationWarnings > 0) {
        check(false, std::to_string(result.info.validationErrors) + " validation error(s), " +
                         std::to_string(result.info.validationWarnings) + " warning(s)");
    }
    return result;
}

float maxSpeed(const fluid::FluidFeature::Readback &r) {
    float speed = 0.0f;
    for (const phox::vec2 &u : r.velocity) speed = std::max(speed, glm::length(u));
    return speed;
}

bool finite(const fluid::FluidFeature::Readback &r) {
    for (float d : r.density) if (!std::isfinite(d)) return false;
    for (const phox::vec2 &u : r.velocity) if (!std::isfinite(u.x) || !std::isfinite(u.y)) return false;
    return true;
}

// The root mean square of the velocity's divergence, discretized as fluid_sf_divergence.comp does,
// relative to the root mean square of its gradient's size (so it does not depend on how fast it moves).
double relativeDivergence(const fluid::FluidFeature::Readback &r) {
    const int n = static_cast<int>(r.resolution);
    const auto at = [&](int x, int y) {
        return x < 0 || y < 0 || x >= n || y >= n ? phox::vec2(0.0f) : r.velocity[static_cast<std::size_t>(y * n + x)];
    };
    double divergence = 0.0, gradient = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const phox::vec2 dx = 0.5f * static_cast<float>(n) * (at(x + 1, y) - at(x - 1, y));
            const phox::vec2 dy = 0.5f * static_cast<float>(n) * (at(x, y + 1) - at(x, y - 1));
            const double div = dx.x + dy.y;
            divergence += div * div;
            gradient += glm::dot(dx, dx) + glm::dot(dy, dy);
        }
    }
    return std::sqrt(divergence / std::max(gradient, 1e-30));
}

bool sameFields(const fluid::FluidFeature::Readback &a, const fluid::FluidFeature::Readback &b) {
    return a.density.size() == b.density.size() &&
           std::memcmp(a.density.data(), b.density.data(), a.density.size() * sizeof(float)) == 0 &&
           std::memcmp(a.velocity.data(), b.velocity.data(), a.velocity.size() * sizeof(phox::vec2)) == 0;
}

std::size_t differingPixels(const phox::CpuImage &a, const phox::CpuImage &b) {
    std::size_t count = 0;
    for (std::size_t p = 0; p + 3 < a.rgba.size() && p + 3 < b.rgba.size(); p += 4) {
        if (std::memcmp(&a.rgba[p], &b.rgba[p], 3) != 0) ++count;
    }
    return count + (a.rgba.size() != b.rgba.size() ? 1 : 0);
}

} // namespace

int main() {
    const MakeSolver stable = [] { return std::make_unique<fluid::StableFluids>(); };
    const MakeSolver lbm = [] { return std::make_unique<fluid::LatticeBoltzmann>(); };

    // At rest.
    const Result stableRest = run(stable, 5, false);
    if (stableRest.info.backend != phox::RendererBackend::Vulkan) {
        std::cerr << "no Vulkan device available" << std::endl;
        return envFlag("PHOX_REQUIRE_GPU") ? 1 : kSkip;
    }
    std::cout << "on " << stableRest.info.deviceName << (stableRest.info.asyncCompute ? " with" : " without")
              << " an async compute queue" << std::endl;
    check(maxSpeed(stableRest.fields) == 0.0f, "stable fluids stays at rest without forces");
    const Result lbmRest = run(lbm, 5, false, true, true, 4);
    float densityError = 0.0f;
    for (float d : lbmRest.fields.density) densityError = std::max(densityError, std::abs(d - 1.0f));
    check(maxSpeed(lbmRest.fields) < 1e-6f && densityError < 1e-6f, "lattice Boltzmann stays at rest without forces");

    // Stable fluids: projection.
    for (std::uint32_t iterations : {0u, 40u, 400u}) {
        fluid::StableFluids::Settings settings;
        settings.iterations = iterations;
        const Result r = run([settings] { return std::make_unique<fluid::StableFluids>(settings); }, 30, true);
        std::cout << "  stable fluids, " << iterations << " Jacobi iterations: relative divergence "
                  << relativeDivergence(r.fields) << ", max speed " << maxSpeed(r.fields) << std::endl;
        check(finite(r.fields) && maxSpeed(r.fields) > 0.05f, "stable fluids moves when stirred (" +
                                                                  std::to_string(iterations) + " iterations)");
        if (iterations == 0) {
            check(relativeDivergence(r.fields) > 0.2, "without projection the stirred velocity diverges");
        } else if (iterations == 400) {
            check(relativeDivergence(r.fields) < 0.02, "with projection it is nearly divergence free");
        }
    }

    // Lattice Boltzmann: mass and the speed limit.
    const Result lbmStirred = run(lbm, 60, true, true, true, 4);
    double mass = 0.0;
    for (float d : lbmStirred.fields.density) mass += d;
    const double cells = static_cast<double>(kResolution) * kResolution;
    float minDensity = 10.0f, maxDensity = 0.0f;
    for (float d : lbmStirred.fields.density) minDensity = std::min(minDensity, d), maxDensity = std::max(maxDensity, d);
    std::cout << "  lattice Boltzmann: mass " << mass << " of " << cells << ", density " << minDensity << " to "
              << maxDensity << ", max speed " << maxSpeed(lbmStirred.fields) << " (lattice units)" << std::endl;
    check(finite(lbmStirred.fields), "lattice Boltzmann stays finite when stirred");
    check(std::abs(mass - cells) / cells < 1e-4, "lattice Boltzmann conserves mass");
    check(maxSpeed(lbmStirred.fields) > 1e-3f && maxSpeed(lbmStirred.fields) < 0.15f,
          "lattice Boltzmann moves when stirred, below the lattice's speed limit");

    // The queues compute the same thing.
    for (const auto &[name, make] : {std::pair{"stable fluids", stable}, std::pair{"lattice Boltzmann", lbm}}) {
        const Result async = run(make, 20, true, true);
        const Result graphics = run(make, 20, true, false);
        check(sameFields(async.fields, graphics.fields),
              std::string(name) + ": the async compute queue and the graphics queue agree bit for bit");
    }

    // Pipelining shows the previous frame's surface.
    for (const auto &[name, make] : {std::pair{"stable fluids", stable}, std::pair{"lattice Boltzmann", lbm}}) {
        const Result pipelined = run(make, 21, true, true, true);
        const Result immediate = run(make, 20, true, true, false);
        const std::size_t differing = differingPixels(pipelined.image, immediate.image);
        check(differing == 0, std::string(name) + ": pipelined frame 21 shows frame 20's surface (" +
                                  std::to_string(differing) + " pixels differ)");
    }

    std::cout << (failures == 0 ? "fluid tests passed" : "fluid tests FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
