#include "phox/forward/DrawMeshes.hpp"

#include "phox/forward/UploadMeshes.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef PHOX_SHADER_DIR
#error "PHOX_SHADER_DIR must be defined by the build"
#endif

namespace phox::forward {

namespace {

std::vector<uint32_t> readSpirv(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open shader file: " + path.string());
    }
    const std::streamsize bytes = in.tellg();
    if (bytes <= 0 || bytes % 4 != 0) {
        throw std::runtime_error("not a SPIR-V file: " + path.string());
    }
    std::vector<uint32_t> words(static_cast<size_t>(bytes) / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(words.data()), bytes);
    if (!in) {
        throw std::runtime_error("failed to read shader file: " + path.string());
    }
    return words;
}

} // namespace

void DrawMeshes::init(rhi::Device &device, rhi::Format colorFormat) {
    shutdown(device);

    const std::filesystem::path shaderDir = PHOX_SHADER_DIR;
    const auto vertexCode = readSpirv(shaderDir / "basic.vert.spv");
    const auto fragmentCode = readSpirv(shaderDir / "basic.frag.spv");
    m_vertexShader = device.createShader({rhi::ShaderStage::Vertex, vertexCode});
    m_fragmentShader = device.createShader({rhi::ShaderStage::Fragment, fragmentCode});

    rhi::GraphicsPipelineDesc desc;
    desc.shaders = {m_vertexShader, m_fragmentShader};
    desc.vertexLayout = {sizeof(Vertex),
                         {{0, rhi::VertexFormat::Float32x3, offsetof(Vertex, position)},
                          {1, rhi::VertexFormat::Float32x3, offsetof(Vertex, normal)},
                          {2, rhi::VertexFormat::Float32x2, offsetof(Vertex, texCoord)}}};
    desc.colorFormats = {colorFormat};
    desc.bindings = {{0, 0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex}};
    desc.pushConstants = {{rhi::ShaderStage::Fragment, 0, sizeof(float) * 4}};
    desc.debugName = "forward";
    try {
        m_pipeline = device.createGraphicsPipeline(desc);
    } catch (...) {
        shutdown(device);
        throw;
    }
}

void DrawMeshes::shutdown(rhi::Device &device) {
    device.destroy(m_pipeline);
    device.destroy(m_fragmentShader);
    device.destroy(m_vertexShader);
    m_pipeline = {};
    m_fragmentShader = {};
    m_vertexShader = {};
}

DrawMeshes::Out DrawMeshes::setup(graph::GraphBuilder &builder, const In &in) {
    if (!m_pipeline.valid()) {
        throw std::logic_error("DrawMeshes::setup() called before init()");
    }
    const rhi::PipelineHandle pipeline = m_pipeline;
    const graph::GraphTexture target = in.target;
    const graph::GraphBuffer vertices = in.vertices;
    const graph::GraphBuffer camera = in.camera;
    const uint32_t vertexCount = in.vertexCount;
    const std::array<float, 4> albedo = in.albedo;
    const bool draws = vertexCount > 0 && vertices.valid() && camera.valid();

    builder.addPass(
        "draw meshes",
        [=](graph::PassBuilder &pass) {
            pass.colorTarget(target, true, {0.1f, 0.2f, 0.3f, 1.0f});
            if (draws) {
                pass.read(vertices, graph::BufferAccess::Vertex);
                pass.read(camera, graph::BufferAccess::Uniform);
            }
        },
        [=](graph::PassContext &context) {
            if (!draws) {
                return;
            }
            context.cmd.bindPipeline(pipeline);
            context.cmd.bindVertexBuffer(context.buffer(vertices));
            context.cmd.bindUniformBuffer(0, 0, context.buffer(camera));
            context.cmd.pushConstants(rhi::ShaderStage::Fragment, 0, std::as_bytes(std::span(albedo)));
            context.cmd.draw(vertexCount);
        });
    return {target};
}

} // namespace phox::forward
