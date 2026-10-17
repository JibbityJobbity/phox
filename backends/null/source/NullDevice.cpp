#include "phox/null/NullDevice.hpp"

#include <algorithm>

namespace phox::null {

NullDevice::NullDevice() {
    m_capabilities.deviceName = "Null";
    m_capabilities.maxTextureSize2D = 16384;
    m_capabilities.maxPushConstantBytes = 128;
}

const char *NullDevice::keepName(const char *name) {
    if (name == nullptr) {
        return nullptr;
    }
    m_names.emplace_back(name);
    return m_names.back().c_str();
}

rhi::BufferHandle NullDevice::createBuffer(const rhi::BufferDesc &desc) {
    if (desc.size == 0) {
        throw std::invalid_argument("createBuffer: size must be greater than zero");
    }
    BufferData data;
    data.desc = desc;
    data.desc.debugName = keepName(desc.debugName);
    if (desc.memory != rhi::MemoryKind::GpuOnly) {
        data.memory.assign(static_cast<size_t>(desc.size), std::byte{0});
    }
    return m_buffers.create(std::move(data));
}

rhi::TextureHandle NullDevice::createTexture(const rhi::TextureDesc &desc) {
    if (desc.width == 0 || desc.height == 0 || desc.mips == 0) {
        throw std::invalid_argument("createTexture: width, height and mips must be greater than zero");
    }
    if (desc.format == rhi::Format::Undefined) {
        throw std::invalid_argument("createTexture: format must not be Undefined");
    }
    TextureData data;
    data.desc = desc;
    data.desc.debugName = keepName(desc.debugName);
    return m_textures.create(std::move(data));
}

rhi::ShaderHandle NullDevice::createShader(const rhi::ShaderDesc &desc) {
    if (desc.spirv.empty()) {
        throw std::invalid_argument("createShader: spirv must not be empty");
    }
    return m_shaders.create(ShaderData{desc.stage});
}

rhi::PipelineHandle NullDevice::createGraphicsPipeline(const rhi::GraphicsPipelineDesc &desc) {
    bool hasVertex = false, hasMesh = false, hasFragment = false;
    std::vector<rhi::ShaderStage> seen;
    for (const rhi::ShaderHandle shader : desc.shaders) {
        const rhi::ShaderStage stage = m_shaders.get(shader).stage;
        if (std::find(seen.begin(), seen.end(), stage) != seen.end()) {
            throw std::invalid_argument("createGraphicsPipeline: duplicate shader stage");
        }
        seen.push_back(stage);
        hasVertex |= stage == rhi::ShaderStage::Vertex;
        hasMesh |= stage == rhi::ShaderStage::Mesh;
        hasFragment |= stage == rhi::ShaderStage::Fragment;
        if (stage == rhi::ShaderStage::Compute) {
            throw std::invalid_argument("createGraphicsPipeline: compute shader in a graphics pipeline");
        }
    }
    if (!hasFragment || (!hasVertex && !hasMesh)) {
        throw std::invalid_argument("createGraphicsPipeline: needs a Fragment shader and a Vertex or Mesh shader");
    }
    return m_pipelines.create(PipelineData{false});
}

rhi::PipelineHandle NullDevice::createComputePipeline(const rhi::ComputePipelineDesc &desc) {
    if (m_shaders.get(desc.shader).stage != rhi::ShaderStage::Compute) {
        throw std::invalid_argument("createComputePipeline: shader is not a Compute shader");
    }
    return m_pipelines.create(PipelineData{true});
}

void NullDevice::destroy(rhi::BufferHandle handle) { m_buffers.destroy(handle); }
void NullDevice::destroy(rhi::TextureHandle handle) { m_textures.destroy(handle); }
void NullDevice::destroy(rhi::ShaderHandle handle) { m_shaders.destroy(handle); }
void NullDevice::destroy(rhi::PipelineHandle handle) { m_pipelines.destroy(handle); }

std::span<std::byte> NullDevice::map(rhi::BufferHandle handle) {
    BufferData &data = m_buffers.get(handle);
    if (data.desc.memory == rhi::MemoryKind::GpuOnly) {
        assert(false && "map() requires Upload or Readback memory");
        throw std::logic_error("map: buffer memory is GpuOnly");
    }
    return std::span<std::byte>(data.memory);
}

const rhi::TextureDesc &NullDevice::describe(rhi::TextureHandle handle) const { return m_textures.get(handle).desc; }
const rhi::BufferDesc &NullDevice::describe(rhi::BufferHandle handle) const { return m_buffers.get(handle).desc; }

rhi::CommandList &NullDevice::beginCommands() {
    m_current = std::make_unique<NullCommandList>(m_capabilities);
    return *m_current;
}

rhi::SubmitTicket NullDevice::submit(rhi::CommandList &commands) {
    if (m_current == nullptr || &commands != m_current.get()) {
        throw std::logic_error("submit: command list was not returned by the latest beginCommands()");
    }
    m_lastRecording = m_current->takeRecording();
    m_current.reset();
    return rhi::SubmitTicket{++m_ticket};
}

} // namespace phox::null
