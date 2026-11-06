// GPU-generated geometry (vk::FrameFeature, GeneratedDraw): vk::WaveSurface builds a surface in a compute
// pass, on the async compute queue where there is one, and the forward pass draws it indirectly. The
// image must match the same surface built on the CPU (WaveSurface::referenceMesh) and drawn as a scene
// object, with async compute on and off, with vertex and with mesh shaders for the rest of the scene.
//
// Environment: PHOX_VALIDATION=1 (fail on any validation message), PHOX_REQUIRE_GPU=1 (fail instead of
// skipping without a Vulkan device), PHOX_DEVICE=<name>.
#include "phox/CpuImage.hpp"
#include "phox/Scene.hpp"
#include "phox/vulkan/Renderer.hpp"
#include "phox/vulkan/WaveSurface.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr int kSkip = 77;
constexpr int kFrames = 4; // frames in flight and pooled buffers are reused before the last is compared
constexpr float kTime = 0.9f;
const phox::Extent2D kExtent{320, 240};

bool envFlag(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

phox::Material paint(float r, float g, float b, float roughness) {
    phox::Material material;
    material.albedo = {r, g, b, 1.0f};
    material.roughness = roughness;
    return material;
}

// A cube standing in the waves, so depth testing between the scene and the generated surface matters.
phox::Scene makeScene() {
    phox::Scene scene;
    auto cube = std::make_shared<phox::Mesh>(phox::Mesh::cube(1.0f));
    scene.addObject("cube", cube, paint(0.9f, 0.3f, 0.2f, 0.4f), phox::Transform{{0.3f, 0.1f, 0.2f}});
    scene.camera().position = {0.0f, 2.4f, 4.2f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    scene.light().direction = {-0.5f, -1.0f, -0.35f};
    return scene;
}

phox::vk::WaveSurface::Settings waveSettings(bool async) {
    phox::vk::WaveSurface::Settings settings;
    settings.resolution = 40; // not a multiple of the shader's 8x8 groups: partial groups must stay out
    settings.material = paint(0.15f, 0.4f, 0.8f, 0.2f);
    settings.model = glm::rotate(phox::mat4(1.0f), 0.3f, phox::vec3(0.0f, 1.0f, 0.0f));
    settings.async = async;
    return settings;
}

struct Run {
    phox::CpuImage image;
    phox::RendererInfo info;
    std::string graph;
};

// Renders the scene with the waves either generated (a WaveSurface feature) or as a scene object.
Run render(bool generated, bool asyncCompute, bool meshShaders) {
    phox::vk::Renderer renderer;
    phox::RendererConfig config;
    config.debug = envFlag("PHOX_VALIDATION");
    config.headlessExtent = kExtent;
    config.meshShaders = meshShaders;
    config.asyncCompute = asyncCompute;
    config.captureFrameGraph = true;
    renderer.initialize(config);

    phox::Scene scene = makeScene();
    const phox::vk::WaveSurface::Settings settings = waveSettings(true);
    if (generated) {
        auto waves = std::make_shared<phox::vk::WaveSurface>(settings);
        waves->setTime(kTime);
        renderer.addFeature(waves);
    } else {
        // The model matrix as a transform: a rotation about y.
        phox::Transform transform;
        transform.rotation = glm::angleAxis(0.3f, phox::vec3(0.0f, 1.0f, 0.0f));
        scene.addObject("waves", std::make_shared<phox::Mesh>(phox::vk::WaveSurface::referenceMesh(settings, kTime)),
                        settings.material, transform);
    }
    for (int i = 0; i < kFrames; ++i) renderer.render(scene);
    Run run{renderer.lastFrame(), renderer.info(), renderer.frameGraphDot()};
    renderer.cleanup();
    return run;
}

// The pixels where the images differ by more than 2 in a channel, which must be at most 0.5%: the GPU's
// sin and cos are not the CPU's, so the two surfaces differ in the last bits.
bool similar(const phox::CpuImage &a, const phox::CpuImage &b, const std::string &what) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size() || a.rgba.empty()) {
        std::cerr << "FAIL " << what << ": image sizes differ\n";
        return false;
    }
    std::size_t over = 0;
    int maxDelta = 0;
    for (std::size_t p = 0; p < a.rgba.size(); p += 4) {
        int delta = 0;
        for (int c = 0; c < 3; ++c) delta = std::max(delta, std::abs(int(a.rgba[p + c]) - int(b.rgba[p + c])));
        maxDelta = std::max(maxDelta, delta);
        if (delta > 2) ++over;
    }
    const std::size_t allowed = a.rgba.size() / 4 / 200;
    std::cout << (over <= allowed ? "PASS " : "FAIL ") << what << ": max channel delta " << maxDelta << ", " << over
              << " pixel(s) over tolerance (allowed " << allowed << ")\n";
    return over <= allowed;
}

// Waves cover a good part of the image: a draw that silently drew nothing would match an empty scene.
bool showsWaves(const phox::CpuImage &withWaves, const phox::CpuImage &without) {
    std::size_t changed = 0;
    for (std::size_t p = 0; p < withWaves.rgba.size(); p += 4) {
        for (int c = 0; c < 3; ++c) {
            if (std::abs(int(withWaves.rgba[p + c]) - int(without.rgba[p + c])) > 8) {
                ++changed;
                break;
            }
        }
    }
    return changed > withWaves.rgba.size() / 4 / 5;
}

} // namespace

int main() {
    int failures = 0;
    bool anyGpu = false;
    for (bool meshShaders : {false, true}) {
        for (bool asyncCompute : {true, false}) {
            const std::string what = std::string(meshShaders ? "mesh shaders" : "vertex shaders") +
                                     (asyncCompute ? ", async compute" : ", one queue");
            const Run generated = render(true, asyncCompute, meshShaders);
            if (generated.info.backend != phox::RendererBackend::Vulkan) {
                std::cerr << "no Vulkan device available" << std::endl;
                return envFlag("PHOX_REQUIRE_GPU") ? 1 : kSkip;
            }
            anyGpu = true;
            if (meshShaders && !generated.info.meshShaders) {
                std::cout << "SKIP " << what << ": the device has no mesh shaders\n";
                continue;
            }
            const Run reference = render(false, asyncCompute, meshShaders);
            failures += similar(generated.image, reference.image, what) ? 0 : 1;

            // The pass ran where it should: on the compute queue exactly when the device has one.
            const bool onAsync = generated.graph.find("wave surface\\ncompute (async)") != std::string::npos;
            if (onAsync != generated.info.asyncCompute) {
                std::cerr << "FAIL " << what << ": the wave pass ran " << (onAsync ? "async" : "on the graphics queue")
                          << ", but the device " << (generated.info.asyncCompute ? "has" : "has no")
                          << " async compute queue\n";
                ++failures;
            }
            for (const Run *run : {&generated, &reference}) {
                if (run->info.validationErrors + run->info.validationWarnings > 0) {
                    std::cerr << "FAIL " << what << ": " << run->info.validationErrors << " validation error(s), "
                              << run->info.validationWarnings << " warning(s)\n";
                    ++failures;
                }
            }
        }
    }
    if (anyGpu) {
        // Against the scene without waves at all.
        phox::vk::Renderer renderer;
        phox::RendererConfig config;
        config.headlessExtent = kExtent;
        renderer.initialize(config);
        const phox::Scene scene = makeScene();
        renderer.render(scene);
        const phox::CpuImage empty = renderer.lastFrame();
        renderer.cleanup();
        if (!showsWaves(render(true, true, false).image, empty)) {
            std::cerr << "FAIL the generated waves cover too little of the image\n";
            ++failures;
        }
    }
    std::cout << (failures == 0 ? "generated geometry tests passed" : "generated geometry tests FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
