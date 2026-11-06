#include "fluid/FluidFeature.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/GraphExecutor.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#ifndef PHOX_FLUID_SHADER_DIR
#error "PHOX_FLUID_SHADER_DIR must point at the compiled fluid shaders"
#endif

namespace fluid {

using namespace phox;

namespace {

struct SurfacePush {
    std::uint32_t n;
    float size;
    float base;
    float scale;
};

} // namespace

FluidFeature::FluidFeature(std::unique_ptr<FluidSolver> solver, Settings settings)
    : m_solver(std::move(solver)), m_settings(settings) {
    if (!m_solver) {
        throw std::invalid_argument("FluidFeature needs a solver");
    }
    if (m_settings.resolution < 2) {
        throw std::invalid_argument("FluidFeature needs at least 2 cells a side");
    }
}

FluidFeature::~FluidFeature() = default;

std::vector<std::filesystem::path> FluidFeature::shaderDirectories() const { return {PHOX_FLUID_SHADER_DIR}; }

void FluidFeature::initialize(vk::Device &device, vk::PipelineCache &) {
    const std::uint32_t n = m_settings.resolution;
    m_solver->initialize(device, n);
    const std::uint64_t vertices = static_cast<std::uint64_t>(n - 1) * (n - 1) * 6;
    for (int i = 0; i < 2; ++i) {
        const std::string suffix = " " + std::to_string(i);
        m_vertices[i].create(device, vertices * sizeof(Vertex),
                             usageBit(BufferUsage::StorageWrite) | usageBit(BufferUsage::Vertex), "fluid surface" + suffix);
        m_indirect[i].create(device, sizeof(VkDrawIndirectCommand),
                             usageBit(BufferUsage::StorageWrite) | usageBit(BufferUsage::Indirect), "fluid draw" + suffix);
        m_written[i] = false;
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(n) * n;
    m_readbackDensity = device.makeBuffer(
        {cells * sizeof(float), usageBit(BufferUsage::TransferDst), MemoryLocation::Readback, "fluid density readback"});
    m_readbackVelocity = device.makeBuffer({cells * 2 * sizeof(float), usageBit(BufferUsage::TransferDst),
                                            MemoryLocation::Readback, "fluid velocity readback"});
    m_frames = 0;
    m_readbackPending = false;
}

void FluidFeature::shutdown() {
    m_solver->shutdown();
    for (int i = 0; i < 2; ++i) {
        m_vertices[i].destroy();
        m_indirect[i].destroy();
    }
    m_readbackDensity.reset();
    m_readbackVelocity.reset();
}

void FluidFeature::addPasses(vk::FrameFeatureContext &context, std::vector<vk::GeneratedDraw> &draws) {
    const std::uint32_t n = m_settings.resolution;
    m_solver->beginFrame(context);
    for (std::uint32_t i = 0; i < m_settings.steps; ++i) m_solver->step(context, m_inputs);
    const FluidOutputs outputs = m_solver->outputs();

    // This frame's surface, from the last step's density.
    const int written = m_settings.pipelined ? static_cast<int>(m_frames % 2) : 0;
    BufferHandle vertices = m_vertices[written].import(context, false);
    BufferHandle indirect = m_indirect[written].import(context, false);
    const SurfacePush surface{n, m_settings.size, m_solver->restDensity(), m_settings.heightScale};
    dispatchGrid(context, "fluid surface", "fluid_surface.comp", {outputs.density}, {&vertices, &indirect}, surface, n, n);
    m_written[written] = true;

    if (m_settings.pipelined) {
        // Last frame's surface, which the async queue finished before this frame's work there.
        const int shown = 1 - written;
        if (m_written[shown]) {
            draws.push_back({m_vertices[shown].import(context, true), m_indirect[shown].import(context, true),
                             m_settings.model, m_settings.material});
        }
    } else {
        draws.push_back({vertices, indirect, m_settings.model, m_settings.material});
    }

    m_readbackPending = false;
    if (m_readbackRequested) {
        m_readbackRequested = false;
        m_readbackPending = true;
        const auto importReadback = [&](const char *name, const vk::Buffer &buffer) {
            const BufferHandle handle = context.graph.importBuffer(name, BufferDesc{buffer.size()}, BufferUsage::Undefined,
                                                                   BufferUsage::HostRead, Queue::AsyncCompute);
            context.executor.bindBuffer(handle, buffer.handle());
            return handle;
        };
        struct Copy {
            BufferHandle density, velocity, densityOut, velocityOut;
        };
        const BufferHandle densityOut = importReadback("fluid density readback", *m_readbackDensity);
        const BufferHandle velocityOut = importReadback("fluid velocity readback", *m_readbackVelocity);
        context.graph.addPass<Copy>(
            "fluid readback", PassType::Compute,
            [&](PassBuilder &builder, Copy &copy) {
                copy.density = builder.read(outputs.density, BufferUsage::TransferSrc);
                copy.velocity = builder.read(outputs.velocity, BufferUsage::TransferSrc);
                copy.densityOut = builder.write(densityOut, BufferUsage::TransferDst);
                copy.velocityOut = builder.write(velocityOut, BufferUsage::TransferDst);
                builder.async();
            },
            [densityBytes = m_readbackDensity->size(), velocityBytes = m_readbackVelocity->size()](
                const Copy &copy, PassContext &passContext) {
                auto &ctx = vk::PassContext::from(passContext);
                const auto &table = ctx.device().table();
                const VkBufferCopy density{0, 0, densityBytes};
                const VkBufferCopy velocity{0, 0, velocityBytes};
                table.vkCmdCopyBuffer(ctx.cmd(), ctx.buffer(copy.density), ctx.buffer(copy.densityOut), 1, &density);
                table.vkCmdCopyBuffer(ctx.cmd(), ctx.buffer(copy.velocity), ctx.buffer(copy.velocityOut), 1, &velocity);
            });
    }
    ++m_frames;
}

std::optional<FluidFeature::Readback> FluidFeature::readback() const {
    if (!m_readbackPending || !m_readbackDensity) {
        return std::nullopt;
    }
    Readback out;
    out.resolution = m_settings.resolution;
    const std::size_t cells = static_cast<std::size_t>(out.resolution) * out.resolution;
    out.density.resize(cells);
    out.velocity.resize(cells);
    m_readbackDensity->invalidate();
    m_readbackVelocity->invalidate();
    std::memcpy(out.density.data(), m_readbackDensity->mapped(), cells * sizeof(float));
    std::memcpy(out.velocity.data(), m_readbackVelocity->mapped(), cells * sizeof(vec2));
    return out;
}

} // namespace fluid
