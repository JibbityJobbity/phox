#include "fluid/LatticeBoltzmann.hpp"

#include <algorithm>
#include <string>

namespace fluid {

using namespace phox;

namespace {

struct StepPush {
    std::uint32_t n;
    float tau;
    std::uint32_t splatCount;
    float forceScale;
    GpuSplat splats[kMaxSplats];
};
struct GridPush {
    std::uint32_t n;
};

} // namespace

void LatticeBoltzmann::initialize(vk::Device &device, std::uint32_t resolution) {
    m_resolution = resolution;
    const std::uint64_t cells = static_cast<std::uint64_t>(resolution) * resolution;
    const BufferUsageMask usage = usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite);
    for (int i = 0; i < 2; ++i) {
        m_populationBuffers[i].create(device, cells * 9 * sizeof(float), usage, "lbm populations " + std::to_string(i));
    }
    m_valid = false;
}

void LatticeBoltzmann::shutdown() {
    for (PersistentBuffer &buffer : m_populationBuffers) buffer.destroy();
}

void LatticeBoltzmann::beginFrame(vk::FrameFeatureContext &context) {
    for (int i = 0; i < 2; ++i) m_populations[i] = m_populationBuffers[i].import(context, m_valid && i == m_current);
    if (!m_valid) {
        dispatchGrid(context, "lbm init", "fluid_lbm_init.comp", {}, {&m_populations[m_current]}, GridPush{m_resolution},
                     m_resolution, m_resolution);
        m_valid = true;
    }
}

void LatticeBoltzmann::step(vk::FrameFeatureContext &context, const FluidInputs &inputs) {
    const std::uint32_t n = m_resolution;
    StepPush push{n, m_settings.tau, static_cast<std::uint32_t>(std::min(inputs.splats.size(), kMaxSplats)),
                  m_settings.forceScale, {}};
    for (std::uint32_t i = 0; i < push.splatCount; ++i) {
        const Splat &s = inputs.splats[i];
        push.splats[i] = {vec4(s.position, s.force), vec4(s.radius, s.amount, 0.0f, 0.0f)};
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(n) * n;
    m_outputs.density = context.graph.createBuffer("lbm density", BufferDesc{cells * sizeof(float)});
    m_outputs.velocity = context.graph.createBuffer("lbm velocity", BufferDesc{cells * 2 * sizeof(float)});
    dispatchGrid(context, "lbm step", "fluid_lbm_step.comp", {m_populations[m_current]},
                 {&m_populations[1 - m_current], &m_outputs.density, &m_outputs.velocity}, push, n, n);
    m_current = 1 - m_current;
}

} // namespace fluid
