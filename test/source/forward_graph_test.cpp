// The forward technique on the null backend: checks the shape of the recorded frame without a GPU.

#include "Check.hpp"

#include "phox/forward/ForwardTechnique.hpp"
#include "phox/graph/RenderGraph.hpp"
#include "phox/null/NullDevice.hpp"
#include "phox/scene/Camera.hpp"
#include "phox/scene/Scene.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace phox;

namespace {

std::vector<std::string> record(null::NullDevice &device, forward::ForwardTechnique &technique, graph::RenderGraph &renderGraph,
                                const scene::Scene &scene, std::string *dumpOut = nullptr) {
    const rhi::TextureHandle target = device.createTexture(
        {640, 480, 1, rhi::Format::RGBA8Unorm, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc, "target"});
    graph::GraphBuilder &builder = renderGraph.begin();
    const graph::GraphTexture imported = builder.importTexture(target, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSrc);
    const scene::Camera camera = scene::Camera::unitOrtho();
    technique.setFrame(scene, camera, imported);
    technique.build(builder);
    renderGraph.compile();
    if (dumpOut != nullptr) {
        *dumpOut = renderGraph.dump();
    }
    renderGraph.execute();

    // The readback buffer is sized from the target: 640 x 480 x 4 bytes.
    CHECK(device.describe(renderGraph.resolve(technique.readbackBuffer())).size == 640ull * 480 * 4);
    device.destroy(target);
    return device.lastRecording();
}

} // namespace

int main() {
    null::NullDevice device;
    forward::ForwardTechnique technique;
    technique.init(device); // loads the compiled basic shaders and creates the pipeline
    graph::RenderGraph renderGraph(device);

    scene::Scene scene;
    scene.addObject(scene::ObjectFactory::createTriangle("triangle", scene::Material{"basic", "default"}));
    std::string dump;
    const std::vector<std::string> expected{"beginDebugLabel:upload meshes", "endDebugLabel",
                                            "barrier",
                                            "beginDebugLabel:draw meshes", "beginRendering", "bindPipeline", "bindVertexBuffer",
                                            "bindUniformBuffer", "pushConstants", "draw", "endRendering", "endDebugLabel",
                                            "barrier",
                                            "beginDebugLabel:readback", "copyTextureToBuffer", "endDebugLabel"};
    CHECK(record(device, technique, renderGraph, scene, &dump) == expected);
    CHECK(dump.find("\"upload meshes\" live") != std::string::npos);
    CHECK(dump.find("Undefined -> ColorAttachment") != std::string::npos);
    CHECK(dump.find("ColorAttachment -> TransferSrc") != std::string::npos);

    // An empty scene: no upload pass, no bindings, no draw; the target is still cleared and read back.
    const scene::Scene empty;
    const std::vector<std::string> expectedEmpty{"barrier",
                                                 "beginDebugLabel:draw meshes", "beginRendering", "endRendering", "endDebugLabel",
                                                 "barrier",
                                                 "beginDebugLabel:readback", "copyTextureToBuffer", "endDebugLabel"};
    CHECK(record(device, technique, renderGraph, empty) == expectedEmpty);

    // After shutdown the pipeline is gone, so building a frame is an error rather than a use-after-free.
    technique.shutdown(device);
    CHECK_THROWS_WITH(technique.build(renderGraph.begin()), std::logic_error, "init()");

    // A technique that was never given a frame cannot build one.
    forward::ForwardTechnique fresh;
    CHECK_THROWS_WITH(fresh.build(renderGraph.begin()), std::logic_error, "setFrame");

    std::cout << "forward graph test passed" << std::endl;
    return 0;
}
