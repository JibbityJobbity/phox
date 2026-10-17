// The baseline frame through the forward technique and the render graph, compared against the golden
// image captured before the refactor started.

#include "Check.hpp"
#include "GpuTest.hpp"
#include "ImageCompare.hpp"

#include "phox/forward/ForwardTechnique.hpp"
#include "phox/graph/RenderGraph.hpp"
#include "phox/scene/Camera.hpp"
#include "phox/scene/Scene.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>

#ifndef PHOX_GOLDEN_DIR
#error "PHOX_GOLDEN_DIR must be defined by the build"
#endif

using namespace phox;

namespace {

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;

test::Image renderFrame(rhi::Device &device, forward::ForwardTechnique &technique, graph::RenderGraph &renderGraph,
                        const scene::Scene &scene, const scene::Camera &camera) {
    const rhi::TextureHandle target = device.createTexture(
        {kWidth, kHeight, 1, rhi::Format::RGBA8Unorm,
         rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc, "target"});

    graph::GraphBuilder &builder = renderGraph.begin();
    const graph::GraphTexture imported =
        builder.importTexture(target, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSrc);
    technique.setFrame(scene, camera, imported);
    technique.build(builder);
    renderGraph.compile(); // throws GraphError if the technique's requirements are not met
    device.wait(renderGraph.execute());

    const std::span<std::byte> pixels = device.map(renderGraph.resolve(technique.readbackBuffer()));
    CHECK(pixels.size() == size_t{kWidth} * kHeight * 4);
    test::Image image;
    image.width = kWidth;
    image.height = kHeight;
    image.rgb.resize(size_t{kWidth} * kHeight * 3);
    for (size_t i = 0; i < size_t{kWidth} * kHeight; ++i) {
        for (size_t c = 0; c < 3; ++c) {
            image.rgb[i * 3 + c] = static_cast<uint8_t>(pixels[i * 4 + c]);
        }
    }
    device.destroy(target);
    return image;
}

} // namespace

int main() {
    test::installLogCapture();
    const test::Image golden = test::loadPpm(std::filesystem::path(PHOX_GOLDEN_DIR) / "triangle_640x480.ppm");

    {
        const auto device = test::createGpuDevice();
        forward::ForwardTechnique technique;
        CHECK(technique.requirements().empty());
        CHECK(technique.isSupported(device->capabilities()));
        technique.init(*device);

        {
            graph::RenderGraph renderGraph(*device);
            const scene::Camera camera = scene::Camera::unitOrtho();

            scene::Scene scene;
            scene.addObject(scene::ObjectFactory::createTriangle("triangle", scene::Material{"basic", "default"}));

            // The baseline frame.
            CHECK(test::matchesGolden(renderFrame(*device, technique, renderGraph, scene, camera), golden));
            // The graph and its pooled resources are reusable: the same frame again gives the same image.
            CHECK(test::matchesGolden(renderFrame(*device, technique, renderGraph, scene, camera), golden));

            // An empty scene still clears the target and nothing else.
            const scene::Scene empty;
            const test::Image cleared = renderFrame(*device, technique, renderGraph, empty, camera);
            const int tolerance = test::goldenTolerance();
            const std::array<int, 3> clearColor{26, 51, 76}; // (0.1, 0.2, 0.3) as 8-bit channels
            for (size_t i = 0; i < cleared.rgb.size(); i += 3) {
                for (size_t c = 0; c < 3; ++c) {
                    CHECK(std::abs(int{cleared.rgb[i + c]} - clearColor[c]) <= tolerance);
                }
            }

            // And the scene can change between frames.
            CHECK(test::matchesGolden(renderFrame(*device, technique, renderGraph, scene, camera), golden));
        }

        technique.shutdown(*device);
    }

    test::checkLogClean();
    std::cout << "forward test passed" << std::endl;
    return 0;
}
