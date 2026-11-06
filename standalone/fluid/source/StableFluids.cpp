#include "fluid/StableFluids.hpp"

#include <algorithm>
#include <string>

namespace fluid {

using namespace phox;

namespace {

constexpr BufferUsageMask kStateUsage = usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                        usageBit(BufferUsage::TransferSrc) | usageBit(BufferUsage::TransferDst);

struct ForcesPush {
    std::uint32_t n;
    float dt;
    std::uint32_t splatCount;
    float pad;
    GpuSplat splats[kMaxSplats];
};
struct AdvectPush {
    std::uint32_t n;
    float dt;
    float velocityDissipation;
    float dyeDissipation;
};
struct GridPush {
    std::uint32_t n;
};

} // namespace

void StableFluids::initialize(vk::Device &device, std::uint32_t resolution) {
    m_resolution = resolution;
    const std::uint64_t cells = static_cast<std::uint64_t>(resolution) * resolution;
    for (int i = 0; i < 2; ++i) {
        const std::string suffix = " " + std::to_string(i);
        m_velocityBuffers[i].create(device, cells * 2 * sizeof(float), kStateUsage, "sf velocity" + suffix);
        m_dyeBuffers[i].create(device, cells * sizeof(float), kStateUsage, "sf dye" + suffix);
        m_pressureBuffers[i].create(device, cells * sizeof(float), kStateUsage, "sf pressure" + suffix);
    }
    m_valid = false;
}

void StableFluids::shutdown() {
    for (auto *buffers : {&m_velocityBuffers, &m_dyeBuffers, &m_pressureBuffers}) {
        for (PersistentBuffer &buffer : *buffers) buffer.destroy();
    }
}

void StableFluids::beginFrame(vk::FrameFeatureContext &context) {
    // The latest of each pair keeps its contents; the other is overwritten before it is read.
    for (int i = 0; i < 2; ++i) {
        m_velocity[i] = m_velocityBuffers[i].import(context, m_valid && i == m_currentVelocity);
        m_dye[i] = m_dyeBuffers[i].import(context, m_valid && i == m_currentDye);
        m_pressure[i] = m_pressureBuffers[i].import(context, m_valid && i == m_currentPressure);
    }
    if (!m_valid) {
        clearBuffer(context, "sf clear velocity", m_velocity[m_currentVelocity]);
        clearBuffer(context, "sf clear dye", m_dye[m_currentDye]);
        clearBuffer(context, "sf clear pressure", m_pressure[m_currentPressure]);
        m_valid = true;
    }
}

void StableFluids::step(vk::FrameFeatureContext &context, const FluidInputs &inputs) {
    const std::uint32_t n = m_resolution;

    ForcesPush forces{n, inputs.dt, static_cast<std::uint32_t>(std::min(inputs.splats.size(), kMaxSplats)), 0.0f, {}};
    for (std::uint32_t i = 0; i < forces.splatCount; ++i) {
        const Splat &s = inputs.splats[i];
        forces.splats[i] = {vec4(s.position, s.force), vec4(s.radius, s.amount, 0.0f, 0.0f)};
    }
    BufferHandle &velocity = m_velocity[m_currentVelocity];
    BufferHandle &dye = m_dye[m_currentDye];
    dispatchGrid(context, "sf forces", "fluid_sf_forces.comp", {}, {&velocity, &dye}, forces, n, n);

    BufferHandle &advectedVelocity = m_velocity[1 - m_currentVelocity];
    BufferHandle &advectedDye = m_dye[1 - m_currentDye];
    const AdvectPush advect{n, inputs.dt, m_settings.velocityDissipation, m_settings.dyeDissipation};
    dispatchGrid(context, "sf advect", "fluid_sf_advect.comp", {velocity, dye}, {&advectedVelocity, &advectedDye},
                 advect, n, n);
    m_currentVelocity = 1 - m_currentVelocity;
    m_currentDye = 1 - m_currentDye;

    if (m_settings.iterations == 0) {
        return;
    }
    BufferHandle divergence = context.graph.createBuffer("sf divergence", BufferDesc{static_cast<std::uint64_t>(n) * n * 4});
    dispatchGrid(context, "sf divergence", "fluid_sf_divergence.comp", {m_velocity[m_currentVelocity]}, {&divergence},
                 GridPush{n}, n, n);
    for (std::uint32_t k = 0; k < m_settings.iterations; ++k) {
        dispatchGrid(context, "sf jacobi", "fluid_sf_jacobi.comp", {m_pressure[m_currentPressure], divergence},
                     {&m_pressure[1 - m_currentPressure]}, GridPush{n}, n, n);
        m_currentPressure = 1 - m_currentPressure;
    }
    dispatchGrid(context, "sf project", "fluid_sf_project.comp", {m_pressure[m_currentPressure]},
                 {&m_velocity[m_currentVelocity]}, GridPush{n}, n, n);
}

} // namespace fluid
