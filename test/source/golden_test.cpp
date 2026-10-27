// Renders known scenes headless and compares them with the images in test/golden/.
//
// Environment:
//   PHOX_UPDATE_GOLDEN=1  write the rendered images to test/golden/ instead of comparing; a
//                         comma-separated list of case names updates only those
//   PHOX_VALIDATION=1     run with the validation layer (incl. synchronization) and fail on any message
//   PHOX_REQUIRE_GPU=1    fail instead of skipping when no Vulkan device is available
//   PHOX_DEVICE=<name>    render on the device whose name contains <name>, e.g. "llvmpipe"
#include "phox/CpuImage.hpp"
#include "phox/Renderer.hpp"
#include "phox/RenderView.hpp"
#include "phox/SampleScenes.hpp"
#include "phox/SoftwareRasterizer.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef PHOX_GOLDEN_DIR
#error "PHOX_GOLDEN_DIR must point at test/golden"
#endif

namespace {

constexpr int kSkip = 77;

// Rasterizers may disagree on pixels an edge passes exactly through, and colour conversion may round
// differently, so allow a small per-channel delta everywhere and larger ones on a few pixels.
constexpr std::uint32_t kChannelTolerance = 2;
constexpr double kMaxFractionOverTolerance = 0.005;

// Each case renders a few frames so the frames in flight and pooled images are reused (and checked by
// validation) before the last frame is compared.
constexpr int kFramesPerCase = 3;

// Whether PHOX_UPDATE_GOLDEN asks for case @p name to be rewritten.
bool updateRequested(const std::string &name) {
    const char *value = std::getenv("PHOX_UPDATE_GOLDEN");
    if (value == nullptr || *value == '\0' || std::strcmp(value, "0") == 0) {
        return false;
    }
    if (std::strcmp(value, "1") == 0) {
        return true;
    }
    const std::string list = std::string(",") + value + ",";
    return list.find("," + name + ",") != std::string::npos;
}

bool envFlag(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

struct GoldenCase {
    std::string name;
    std::string scene;
    float time; ///< animation time the scene is posed at
    phox::ToneMapping toneMapping{};
};

const std::vector<GoldenCase> kCases = {
    {"triangle", "triangle", 0.0f},
    {"triangle_turned", "triangle", 2.0f}, // past 90 degrees: the back face, lit from behind
    {"cubes", "cubes", 0.7f},
    {"cubes_aces", "cubes", 0.7f, {phox::ToneMapOperator::Aces, 1.6f}},
};

} // namespace

int main() {
    const bool validation = envFlag("PHOX_VALIDATION");
    const std::filesystem::path goldenDir = PHOX_GOLDEN_DIR;

    auto renderer = phox::Renderer::Create(phox::RendererType::Vulkan);
    phox::RendererConfig config;
    config.debug = validation;
    config.headlessExtent = {320, 240};
    renderer->initialize(config);

    const phox::RendererInfo info = renderer->info();
    if (info.backend != phox::RendererBackend::Vulkan) {
        std::cerr << "no Vulkan device available" << std::endl;
        return envFlag("PHOX_REQUIRE_GPU") ? 1 : kSkip;
    }
    if (validation && !info.validation) {
        std::cerr << "PHOX_VALIDATION is set but the validation layer is not available" << std::endl;
        return 1;
    }
    std::cout << "device: " << info.deviceName << (validation ? " (validation on)" : "") << std::endl;

    int failures = 0;
    for (const GoldenCase &c : kCases) {
        phox::SampleScene sample = phox::makeSampleScene(c.scene);
        sample.animate(sample.scene, c.time);
        sample.scene.toneMapping = c.toneMapping;
        for (int i = 0; i < kFramesPerCase; ++i) {
            renderer->render(sample.scene);
        }
        const phox::CpuImage &actual = renderer->lastFrame();
        const std::filesystem::path golden = goldenDir / (c.name + ".ppm");

        if (updateRequested(c.name)) {
            phox::writePpm(golden, actual);
            std::cout << "updated " << golden.string() << std::endl;
            continue;
        }

        const phox::CpuImage expected = phox::readPpm(golden);
        const phox::ImageDiff diff = phox::compareImages(actual, expected, kChannelTolerance);
        const std::size_t allowed =
            static_cast<std::size_t>(kMaxFractionOverTolerance * expected.width * expected.height);
        const bool pass = !diff.sizeMismatch && diff.pixelsOverTolerance <= allowed;
        std::cout << (pass ? "PASS " : "FAIL ") << c.name << ": max channel delta " << diff.maxChannelDelta << ", "
                  << diff.pixelsOverTolerance << " pixel(s) over tolerance (allowed " << allowed << ")"
                  << (diff.sizeMismatch ? ", size mismatch" : "") << std::endl;
        if (!pass) {
            const std::filesystem::path out = std::filesystem::path("golden_out") / (c.name + ".ppm");
            phox::writePpm(out, actual);
            std::cout << "  actual image written to " << std::filesystem::absolute(out).string() << std::endl;
            ++failures;
        }

        // The software fallback shares the view maths and lighting, so it should agree except along edges.
        const phox::CpuImage software = phox::rasterize(phox::extractView(sample.scene, config.headlessExtent));
        const phox::ImageDiff softDiff = phox::compareImages(software, expected, kChannelTolerance);
        const bool softPass = !softDiff.sizeMismatch && softDiff.pixelsOverTolerance <= allowed;
        std::cout << (softPass ? "PASS " : "FAIL ") << c.name << " (software): max channel delta "
                  << softDiff.maxChannelDelta << ", " << softDiff.pixelsOverTolerance << " pixel(s) over tolerance (allowed "
                  << allowed << ")" << std::endl;
        if (!softPass) {
            const std::filesystem::path out = std::filesystem::path("golden_out") / (c.name + ".software.ppm");
            phox::writePpm(out, software);
            std::cout << "  software image written to " << std::filesystem::absolute(out).string() << std::endl;
            ++failures;
        }
    }

    const phox::RendererInfo after = renderer->info();
    renderer->cleanup();
    if (validation && (after.validationErrors > 0 || after.validationWarnings > 0)) {
        std::cerr << "validation reported " << after.validationErrors << " error(s) and " << after.validationWarnings
                  << " warning(s)" << std::endl;
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
