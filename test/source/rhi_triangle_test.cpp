// Draws the baseline frame using only rhi:: calls (no render graph), with hand-written barriers, and
// compares it against the golden image captured in phase 0.

#include "Check.hpp"
#include "GpuTest.hpp"
#include "ImageCompare.hpp"

#include "phox/rhi/Backend.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

#ifndef PHOX_SHADER_DIR
#error "PHOX_SHADER_DIR must be defined by the build"
#endif
#ifndef PHOX_TEST_SHADER_DIR
#error "PHOX_TEST_SHADER_DIR must be defined by the build"
#endif
#ifndef PHOX_GOLDEN_DIR
#error "PHOX_GOLDEN_DIR must be defined by the build"
#endif

using namespace phox;

namespace {

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;

struct Vertex {
    float position[3];
    float normal[3];
    float texCoord[2];
};
static_assert(sizeof(Vertex) == 32);

test::Image drawTriangle(rhi::Device &device, bool twoSetPipeline) {
    const std::filesystem::path shaderDir = twoSetPipeline ? PHOX_TEST_SHADER_DIR : PHOX_SHADER_DIR;
    const std::string stem = twoSetPipeline ? "two_sets" : "basic";
    const auto vertCode = test::readSpirv((shaderDir / (stem + ".vert.spv")).string());
    const auto fragCode = test::readSpirv((shaderDir / (stem + ".frag.spv")).string());
    const rhi::ShaderHandle vert = device.createShader({rhi::ShaderStage::Vertex, vertCode});
    const rhi::ShaderHandle frag = device.createShader({rhi::ShaderStage::Fragment, fragCode});

    rhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shaders = {vert, frag};
    pipelineDesc.vertexLayout = {sizeof(Vertex),
                                 {{0, rhi::VertexFormat::Float32x3, 0},
                                  {1, rhi::VertexFormat::Float32x3, 12},
                                  {2, rhi::VertexFormat::Float32x2, 24}}};
    pipelineDesc.colorFormats = {rhi::Format::RGBA8Unorm};
    pipelineDesc.debugName = "triangle";
    if (twoSetPipeline) {
        // The camera lives in set 0 and the albedo in set 1: two sets cannot use push descriptors.
        pipelineDesc.bindings = {{0, 0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex},
                                 {1, 0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Fragment}};
    } else {
        pipelineDesc.bindings = {{0, 0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex}};
        pipelineDesc.pushConstants = {{rhi::ShaderStage::Fragment, 0, 16}};
    }
    const rhi::PipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);

    // Same data as the legacy renderer: a unit triangle and a matrix that maps the scene into clip space.
    const std::array<Vertex, 3> vertices{{{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                                          {{1, 0, 0}, {0, 0, 1}, {0, 0}},
                                          {{0, 1, 0}, {0, 0, 1}, {0, 0}}}};
    const std::array<float, 16> viewProjection{2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<float, 4> albedo{1.0f, 0.2f, 0.4f, 1.0f};

    const rhi::BufferHandle vertexBuffer =
        device.createBuffer({sizeof(vertices), rhi::BufferUsage::Vertex, rhi::MemoryKind::Upload, "vertices"});
    const rhi::BufferHandle cameraBuffer =
        device.createBuffer({sizeof(viewProjection), rhi::BufferUsage::Uniform, rhi::MemoryKind::Upload, "camera"});
    const rhi::BufferHandle albedoBuffer =
        device.createBuffer({sizeof(albedo), rhi::BufferUsage::Uniform, rhi::MemoryKind::Upload, "albedo"});
    std::memcpy(device.map(vertexBuffer).data(), vertices.data(), sizeof(vertices));
    std::memcpy(device.map(cameraBuffer).data(), viewProjection.data(), sizeof(viewProjection));
    std::memcpy(device.map(albedoBuffer).data(), albedo.data(), sizeof(albedo));

    const rhi::TextureHandle target = device.createTexture(
        {kWidth, kHeight, 1, rhi::Format::RGBA8Unorm,
         rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc, "target"});
    const rhi::BufferHandle readback = device.createBuffer(
        {uint64_t{kWidth} * kHeight * 4, rhi::BufferUsage::TransferDst, rhi::MemoryKind::Readback, "readback"});

    rhi::CommandList &cmd = device.beginCommands();
    cmd.beginDebugLabel("triangle");
    const std::array<rhi::TextureBarrier, 1> toColor{{{target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment}}};
    cmd.barrier(toColor, {});

    rhi::RenderingInfo rendering;
    rendering.colors = {{target, true, {0.1f, 0.2f, 0.3f, 1.0f}}};
    rendering.width = kWidth;
    rendering.height = kHeight;
    cmd.beginRendering(rendering);
    cmd.bindPipeline(pipeline);
    cmd.bindVertexBuffer(vertexBuffer);
    cmd.bindUniformBuffer(0, 0, cameraBuffer);
    if (twoSetPipeline) {
        cmd.bindUniformBuffer(1, 0, albedoBuffer);
    } else {
        cmd.pushConstants(rhi::ShaderStage::Fragment, 0, std::as_bytes(std::span(albedo)));
    }
    cmd.draw(3);
    cmd.endRendering();

    const std::array<rhi::TextureBarrier, 1> toTransfer{{{target, rhi::ResourceState::ColorAttachment, rhi::ResourceState::TransferSrc}}};
    cmd.barrier(toTransfer, {});
    cmd.copyTextureToBuffer(target, readback);
    cmd.endDebugLabel();
    device.wait(device.submit(cmd));

    test::Image image;
    image.width = kWidth;
    image.height = kHeight;
    image.rgb.resize(size_t{kWidth} * kHeight * 3);
    const std::span<std::byte> pixels = device.map(readback);
    CHECK(pixels.size() == size_t{kWidth} * kHeight * 4);
    for (size_t i = 0; i < size_t{kWidth} * kHeight; ++i) {
        for (size_t c = 0; c < 3; ++c) {
            image.rgb[i * 3 + c] = static_cast<uint8_t>(pixels[i * 4 + c]);
        }
    }

    device.destroy(readback);
    device.destroy(target);
    device.destroy(albedoBuffer);
    device.destroy(cameraBuffer);
    device.destroy(vertexBuffer);
    device.destroy(pipeline);
    device.destroy(frag);
    device.destroy(vert);
    return image;
}

} // namespace

int main() {
    test::installLogCapture();
    const test::Image golden = test::loadPpm(std::filesystem::path(PHOX_GOLDEN_DIR) / "triangle_640x480.ppm");

    {
        // An unusable adapter is an error that names the problem; there is no silent fallback.
        rhi::DeviceOptions badAdapter;
        badAdapter.validation = test::validationRequested();
        badAdapter.adapterIndex = 999;
        const auto createBad = [&] { return rhi::createDevice(rhi::Backend::Vulkan, badAdapter); };
        CHECK_THROWS_WITH(createBad(), std::runtime_error, "out of range");
    }

    {
        const auto device = test::createGpuDevice();
        const rhi::Capabilities &caps = device->capabilities();
        CHECK(!caps.deviceName.empty());
        CHECK(caps.apiMajor == 1 && caps.apiMinor >= 3);
        CHECK(caps.maxTextureSize2D >= kWidth);
        std::cout << "device: " << caps.deviceName << " (Vulkan " << caps.apiMajor << "." << caps.apiMinor << ")\n";

        // Push-descriptor path (one descriptor set).
        CHECK(test::matchesGolden(drawTriangle(*device, false), golden));
        // Allocated-descriptor-set path (two sets). Same picture, so the same golden image.
        CHECK(test::matchesGolden(drawTriangle(*device, true), golden));
        // The device is reusable after destroying everything: draw again.
        CHECK(test::matchesGolden(drawTriangle(*device, false), golden));
    }

    test::checkLogClean();
    std::cout << "rhi triangle test passed" << std::endl;
    return 0;
}
