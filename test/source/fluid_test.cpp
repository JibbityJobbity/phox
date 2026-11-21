// The fluid sample's solvers (standalone/fluid), run on the GPU through vk::Renderer and read back:
// - at rest without forces, both stay at rest;
// - stable fluids' projection leaves the velocity nearly divergence free, far more so than without it;
// - lattice Boltzmann conserves mass while it is stirred;
// - SPH's fields match a brute-force sum over every particle on the CPU (so its neighbour search finds
//   every neighbour), and its particles stay in the tank;
// - both stay finite and move when stirred, within the lattice's speed limit for LBM;
// - the async compute queue and the graphics queue compute the same fields, bit for bit;
// - pipelined frames show the previous frame's surface: frame n + 1 pipelined looks like frame n without;
// - the surface drawn with task and mesh shaders (tiles built from the heights) is the surface drawn from
//   compute-written vertices, pixel for pixel, whole or cut by the edge of the view, and with its far tiles
//   coarsened it stays close to it. (That tiles of different detail meet without cracks, by moving the finer
//   tile's edge points onto the coarser one's edge, is not tested: the steps a missing fix leaves between
//   tiles are sub-pixel from above, and no view of this surface made the images differ without it.)
//
// Environment: PHOX_VALIDATION=1 (fail on any validation message), PHOX_REQUIRE_GPU=1 (fail instead of
// skipping without a Vulkan device), PHOX_DEVICE=<name>.
#include "fluid/FluidFeature.hpp"
#include "fluid/LatticeBoltzmann.hpp"
#include "fluid/SmoothedParticles.hpp"
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
        fluid::FluidInputs inputs = stirred ? stir(f) : fluid::FluidInputs{};
        inputs.dt = 1.0f / 60.0f / static_cast<float>(steps); // a frame is 1/60 s, however many steps
        feature->setInputs(std::move(inputs));
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
    return a.density.size() == b.density.size() && a.particles.size() == b.particles.size() &&
           std::memcmp(a.density.data(), b.density.data(), a.density.size() * sizeof(float)) == 0 &&
           std::memcmp(a.velocity.data(), b.velocity.data(), a.velocity.size() * sizeof(phox::vec2)) == 0 &&
           std::memcmp(a.particles.data(), b.particles.data(), a.particles.size() * sizeof(phox::vec4)) == 0;
}

// SPH's fields as fluid_sph_fields.comp defines them, summed over every particle (no bins), in double.
// The density is relative to the initial lattice's at the same point.
// Returns the largest differences from the GPU's, relative to the rest density and to the fastest particle.
std::pair<double, double> sphFieldError(const fluid::SmoothedParticles &sph, const fluid::FluidFeature::Readback &r) {
    const int n = static_cast<int>(r.resolution);
    const double h = sph.fieldRadius(), mass = sph.particleMass();
    const auto kernel = [&](double dx, double dy) {
        const double d = h * h - (dx * dx + dy * dy);
        return d > 0.0 ? mass * 4.0 / (3.14159265358979 * std::pow(h, 8.0)) * d * d * d : 0.0;
    };
    const int side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(r.particles.size()))));
    double fastest = 1e-9;
    for (const phox::vec4 &p : r.particles) fastest = std::max(fastest, double(glm::length(phox::vec2(p.z, p.w))));
    double densityError = 0.0, velocityError = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double cx = (x + 0.5) / n, cy = (y + 0.5) / n;
            double rho = 0.0, mx = 0.0, my = 0.0, lattice = 0.0;
            for (int qy = 0; qy < side; ++qy) {
                for (int qx = 0; qx < side; ++qx) lattice += kernel(cx - (qx + 0.5) / side, cy - (qy + 0.5) / side);
            }
            for (const phox::vec4 &p : r.particles) {
                const double w = kernel(cx - p.x, cy - p.y);
                if (w == 0.0) continue;
                rho += w;
                mx += w * p.z;
                my += w * p.w;
            }
            const std::size_t cell = static_cast<std::size_t>(y * n + x);
            densityError = std::max(densityError, std::abs((lattice > 0.0 ? rho / lattice : 1.0) - r.density[cell]));
            const double vx = rho > 0.0 ? mx / rho : 0.0, vy = rho > 0.0 ? my / rho : 0.0;
            velocityError = std::max(velocityError, std::hypot(vx - r.velocity[cell].x, vy - r.velocity[cell].y) / fastest);
        }
    }
    return {densityError, velocityError};
}

std::size_t differingPixels(const phox::CpuImage &a, const phox::CpuImage &b) {
    std::size_t count = 0;
    for (std::size_t p = 0; p + 3 < a.rgba.size() && p + 3 < b.rgba.size(); p += 4) {
        if (std::memcmp(&a.rgba[p], &b.rgba[p], 3) != 0) ++count;
    }
    return count + (a.rgba.size() != b.rgba.size() ? 1 : 0);
}

struct SurfaceView {
    bool meshShaders = true;
    float lodErrorPixels = 0.0f; ///< 0: every tile at full detail
    bool culling = true;
    phox::vec3 eye{0.0f, 3.0f, 3.5f};
    phox::vec3 target{0.0f};
    float heightScale = 3.0f;
    float coarse = 4.0f; ///< the lodErrorPixels that gives tiles of different detail in this view
    std::uint32_t resolution = 130; ///< 16 tiles and a partial one of 2 cells
};

struct SurfaceResult {
    phox::CpuImage image;
    phox::RendererInfo info;
};

// The stirred fluid after 12 frames, drawn as @p view says.
SurfaceResult runSurface(const SurfaceView &view) {
    fluid::FluidFeature::Settings settings;
    settings.resolution = view.resolution;
    settings.heightScale = view.heightScale;
    settings.meshShaders = view.meshShaders;
    auto feature = std::make_shared<fluid::FluidFeature>(std::make_unique<fluid::StableFluids>(), settings);
    phox::vk::Renderer renderer;
    phox::RendererConfig config;
    config.debug = envFlag("PHOX_VALIDATION");
    config.headlessExtent = {320, 240};
    config.meshletCulling = view.culling;
    config.lodErrorPixels = view.lodErrorPixels;
    renderer.addFeature(feature);
    renderer.initialize(config);
    SurfaceResult result;
    result.info = renderer.info();
    if (result.info.backend != phox::RendererBackend::Vulkan) {
        return result;
    }
    phox::Scene scene = makeScene();
    scene.camera().position = view.eye;
    scene.camera().lookAt(view.target);
    for (int f = 0; f < 12; ++f) {
        feature->setInputs(stir(f));
        renderer.render(scene);
    }
    result.image = renderer.lastFrame();
    result.info = renderer.info();
    renderer.cleanup();
    if (result.info.validationErrors + result.info.validationWarnings > 0) {
        check(false, std::to_string(result.info.validationErrors) + " validation error(s), " +
                         std::to_string(result.info.validationWarnings) + " warning(s)");
    }
    return result;
}

// Pixels differing by more than @p threshold in a channel.
std::size_t pixelsOver(const phox::CpuImage &a, const phox::CpuImage &b, int threshold) {
    std::size_t count = 0;
    for (std::size_t p = 0; p + 3 < a.rgba.size() && p + 3 < b.rgba.size(); p += 4) {
        int delta = 0;
        for (int c = 0; c < 3; ++c) delta = std::max(delta, std::abs(int(a.rgba[p + c]) - int(b.rgba[p + c])));
        if (delta > threshold) ++count;
    }
    return count;
}

// Whether the tiles' surface matches the vertices' (the mesh shader path against the vertex path).
void meshSurface(const phox::RendererInfo &info) {
    if (!info.meshShaders) {
        std::cout << "SKIP the mesh shader surface: the renderer has no mesh shaders" << std::endl;
        if (envFlag("PHOX_REQUIRE_MESH_SHADERS")) check(false, "mesh shaders are required but not available");
        return;
    }
    const SurfaceView whole;
    SurfaceView edge; // close and off to one side: tiles on every side of the view's edges
    edge.eye = {0.8f, 1.2f, 1.6f};
    edge.target = {1.0f, 0.0f, 0.4f};
    edge.coarse = 12.0f;
    for (const auto &[name, view] : {std::pair{"whole", whole}, std::pair{"cut by the view's edge", edge}}) {
        SurfaceView vertices = view;
        vertices.meshShaders = false;
        const SurfaceResult reference = runSurface(vertices);
        const SurfaceResult tiles = runSurface(view);
        check(differingPixels(tiles.image, reference.image) == 0,
              std::string("tiles and vertices draw the same surface, ") + name + " (" +
                  std::to_string(differingPixels(tiles.image, reference.image)) + " pixels differ)");
        SurfaceView unculled = view;
        unculled.culling = false;
        check(differingPixels(runSurface(unculled).image, reference.image) == 0,
              std::string("tiles without culling draw it too, ") + name);
        // Far tiles merge cells: some pixels differ, at the silhouette and in the shading, but few by much.
        SurfaceView coarse = view;
        coarse.lodErrorPixels = view.coarse; // near tiles at full detail, farther ones at every 2nd and 4th point
        const SurfaceResult merged = runSurface(coarse);
        const std::size_t any = pixelsOver(merged.image, reference.image, 2);
        const std::size_t far = pixelsOver(merged.image, reference.image, 64);
        const std::size_t pixels = reference.image.rgba.size() / 4;
        std::cout << "  coarse tiles, " << name << ": " << any << " pixels differ, " << far << " by a lot (of " << pixels
                  << ")" << std::endl;
        check(any > 0, std::string("coarse tiles draw fewer points, ") + name);
        check(far * 100 <= pixels, std::string("coarse tiles stay close to the full surface, ") + name);
    }
}

} // namespace

// The mouse: points of the surface, projected to the screen, are found again by pointOnSurface().
void mousePicking() {
    phox::Camera camera;
    camera.position = {0.0f, 3.6f, 4.4f};
    camera.lookAt({0.0f, -0.2f, 0.0f});
    const float aspect = 16.0f / 9.0f, size = 4.0f;
    const phox::mat4 model = glm::translate(phox::mat4(1.0f), phox::vec3(0.3f, 0.0f, -0.2f)) *
                             glm::rotate(phox::mat4(1.0f), 0.4f, phox::vec3(0.0f, 1.0f, 0.0f));
    const phox::mat4 viewProj = camera.projection(aspect) * camera.view() * model;
    float worst = 0.0f;
    bool found = true;
    for (const phox::vec2 &point : {phox::vec2(0.5f), phox::vec2(0.1f, 0.2f), phox::vec2(0.9f, 0.75f), phox::vec2(0.3f, 0.95f)}) {
        const phox::vec4 clip = viewProj * phox::vec4((point.x - 0.5f) * size, 0.0f, (point.y - 0.5f) * size, 1.0f);
        const phox::vec2 screen = (phox::vec2(clip) / clip.w + 1.0f) * 0.5f;
        const auto picked = fluid::pointOnSurface(camera, screen.x, screen.y, aspect, model, size);
        found = found && picked.has_value();
        if (picked) worst = std::max(worst, glm::length(*picked - point));
    }
    const bool missesSky = !fluid::pointOnSurface(camera, 0.5f, 0.0f, aspect, model, size).has_value();
    check(found && worst < 1e-4f && missesSky, "the mouse finds the point of the surface under it (and not the sky)");
}

int main() {
    mousePicking();
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

    // SPH, at a size the CPU can check by brute force: 64 x 64 particles.
    fluid::SmoothedParticles::Settings sphSettings;
    sphSettings.side = 64;
    const MakeSolver sph = [sphSettings] { return std::make_unique<fluid::SmoothedParticles>(sphSettings); };
    const fluid::SmoothedParticles sphReference(sphSettings);
    const Result sphRest = run(sph, 5, false, true, true, 4);
    float restFieldError = 0.0f;
    for (float d : sphRest.fields.density) restFieldError = std::max(restFieldError, std::abs(d - 1.0f));
    check(sphRest.fields.particles.size() == 64u * 64u && maxSpeed(sphRest.fields) < 1e-4f && restFieldError < 1e-4f,
          "SPH stays at rest without forces, and its density field is 1 everywhere");
    const Result sphStirred = run(sph, 40, true, true, true, 8);
    const auto [sphDensityError, sphVelocityError] = sphFieldError(sphReference, sphStirred.fields);
    float fastest = 0.0f, densest = 0.0f, sparsest = 10.0f;
    bool inside = true;
    for (const phox::vec4 &p : sphStirred.fields.particles) {
        fastest = std::max(fastest, glm::length(phox::vec2(p.z, p.w)));
        inside = inside && std::isfinite(p.x) && std::isfinite(p.y) && p.x > 0.0f && p.y > 0.0f && p.x < 1.0f && p.y < 1.0f;
    }
    for (float d : sphStirred.fields.density) densest = std::max(densest, d), sparsest = std::min(sparsest, d);
    std::cout << "  SPH: fastest particle " << fastest << ", density " << sparsest << " to " << densest
              << " x rest; fields against the CPU: "
              << sphDensityError << " (density), " << sphVelocityError << " (velocity, of the fastest)" << std::endl;
    check(inside, "SPH's particles stay in the tank");
    check(fastest > 0.05f && fastest < 2.0f, "SPH moves when stirred, below its speed of sound");
    check(sphDensityError < 1e-4 && sphVelocityError < 1e-4,
          "SPH's fields match a brute-force sum over every particle (the neighbour search misses none)");

    // The queues compute the same thing.
    for (const auto &[name, make] :
         {std::pair{"stable fluids", stable}, std::pair{"lattice Boltzmann", lbm}, std::pair{"SPH", sph}}) {
        const Result async = run(make, 20, true, true);
        const Result graphics = run(make, 20, true, false);
        check(sameFields(async.fields, graphics.fields),
              std::string(name) + ": the async compute queue and the graphics queue agree bit for bit");
    }

    // Pipelining shows the previous frame's surface.
    for (const auto &[name, make] :
         {std::pair{"stable fluids", stable}, std::pair{"lattice Boltzmann", lbm}, std::pair{"SPH", sph}}) {
        const Result pipelined = run(make, 21, true, true, true);
        const Result immediate = run(make, 20, true, true, false);
        const std::size_t differing = differingPixels(pipelined.image, immediate.image);
        check(differing == 0, std::string(name) + ": pipelined frame 21 shows frame 20's surface (" +
                                  std::to_string(differing) + " pixels differ)");
    }

    meshSurface(stableRest.info);

    std::cout << (failures == 0 ? "fluid tests passed" : "fluid tests FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
