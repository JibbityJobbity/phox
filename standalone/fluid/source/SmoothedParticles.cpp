#include "fluid/SmoothedParticles.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace fluid {

using namespace phox;

namespace {

// shaders/fluid_sph_common.glsl
struct SphCommon {
    std::uint32_t side;
    std::uint32_t bins;
    float h;
    float mass;
    float restDensity;
    float stiffness;
    float viscosity;
    float dt;
};
struct IntegratePush {
    SphCommon c;
    std::uint32_t splatCount;
    float forceScale;
    float wallDamping;
    float drag;
    float wallStiffness; // acceleration at the wall
    float splats[kMaxSplats * 6];
};
struct FieldsPush {
    SphCommon c;
    std::uint32_t n;
    float radius; // of the fields' kernel
    float unused;
    std::uint32_t reach; // bins either side to gather from
};
static_assert(sizeof(IntegratePush) <= 128);

constexpr float kPi = 3.14159265358979f;

} // namespace

float SmoothedParticles::poly6(float r2, float h) {
    const float d = h * h - r2;
    return d > 0.0f ? 4.0f / (kPi * std::pow(h, 8.0f)) * d * d * d : 0.0f;
}

namespace {

// The density at a particle in the middle of the initial lattice (spacing apart), with radius h.
float latticeDensityWith(float spacing, float mass, float h) {
    const int reach = static_cast<int>(std::ceil(h / spacing));
    float density = 0.0f;
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            const float r2 = spacing * spacing * static_cast<float>(x * x + y * y);
            density += mass * SmoothedParticles::poly6(r2, h);
        }
    }
    return density;
}

} // namespace

float SmoothedParticles::latticeDensity() const {
    return latticeDensityWith(1.0f / static_cast<float>(m_settings.side), particleMass(), smoothingRadius());
}



std::uint32_t SmoothedParticles::binsPerSide() const {
    // Bins at least h wide, so the neighbours of a particle are in the 3 x 3 bins around it.
    return std::max(1u, static_cast<std::uint32_t>(std::floor(1.0f / smoothingRadius())));
}

void SmoothedParticles::initialize(vk::Device &device, std::uint32_t resolution) {
    m_resolution = resolution;
    const BufferUsageMask usage = usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                  usageBit(BufferUsage::TransferSrc);
    for (int i = 0; i < 2; ++i) {
        m_stateBuffers[i].create(device, std::uint64_t{particleCount()} * 4 * sizeof(float), usage,
                                 "sph particles " + std::to_string(i));
    }
    m_valid = false;
}

void SmoothedParticles::shutdown() {
    for (PersistentBuffer &buffer : m_stateBuffers) buffer.destroy();
}

void SmoothedParticles::beginFrame(vk::FrameFeatureContext &context) {
    for (int i = 0; i < 2; ++i) m_state[i] = m_stateBuffers[i].import(context, m_valid && i == m_current);
    if (!m_valid) {
        const SphCommon common{m_settings.side, binsPerSide(), smoothingRadius(), particleMass(), latticeDensity(),
                               0.0f, 0.0f, 0.0f};
        dispatchGrid(context, "sph init", "fluid_sph_init.comp", {}, {&m_state[m_current]}, common, m_settings.side,
                     m_settings.side);
        m_valid = true;
    }
}

void SmoothedParticles::step(vk::FrameFeatureContext &context, const FluidInputs &inputs) {
    RenderGraph &graph = context.graph;
    const std::uint32_t side = m_settings.side;
    const std::uint32_t bins = binsPerSide();
    const float rest = latticeDensity();
    // Speed of sound c: dp/drho = c^2 at rest.
    const SphCommon common{side,
                           bins,
                           smoothingRadius(),
                           particleMass(),
                           rest,
                           m_settings.soundSpeed * m_settings.soundSpeed * rest,
                           m_settings.viscosity,
                           inputs.dt};
    const std::uint64_t binCount = std::uint64_t{bins} * bins;
    const std::uint64_t particles = particleCount();
    BufferHandle &state = m_state[m_current];

    // Neighbour search.
    BufferHandle counts = graph.createBuffer("sph bin counts", BufferDesc{binCount * 4});
    clearBuffer(context, "sph clear bins", counts);
    dispatchGrid(context, "sph count", "fluid_sph_count.comp", {state}, {&counts}, common, side, side);
    BufferHandle ranges = graph.createBuffer("sph bin ranges", BufferDesc{binCount * 8});
    BufferHandle cursor = graph.createBuffer("sph bin cursor", BufferDesc{binCount * 4});
    dispatchGrid(context, "sph scan", "fluid_sph_scan.comp", {counts}, {&ranges, &cursor}, common, 8, 8); // one 8 x 8 group
    BufferHandle indices = graph.createBuffer("sph sorted indices", BufferDesc{particles * 4});
    dispatchGrid(context, "sph scatter", "fluid_sph_scatter.comp", {state}, {&cursor, &indices}, common, side, side);
    dispatchGrid(context, "sph sort bins", "fluid_sph_sort.comp", {ranges}, {&indices}, common, bins, bins);

    // Density and pressure, then the step.
    BufferHandle pressure = graph.createBuffer("sph density pressure", BufferDesc{particles * 8});
    dispatchGrid(context, "sph density", "fluid_sph_density.comp", {state, ranges, indices}, {&pressure}, common, side,
                 side);
    IntegratePush integrate{common, static_cast<std::uint32_t>(std::min(inputs.splats.size(), kMaxSplats)),
                            m_settings.forceScale, m_settings.wallDamping, m_settings.drag,
                            m_settings.wallStiffness * m_settings.soundSpeed * m_settings.soundSpeed *
                                static_cast<float>(side),
                            {}};
    for (std::uint32_t i = 0; i < integrate.splatCount; ++i) {
        const Splat &s = inputs.splats[i];
        const float values[6] = {s.position.x, s.position.y, s.force.x, s.force.y, s.radius, s.amount};
        std::copy(values, values + 6, integrate.splats + i * 6);
    }
    BufferHandle &next = m_state[1 - m_current];
    dispatchGrid(context, "sph integrate", "fluid_sph_integrate.comp", {state, ranges, indices, pressure}, {&next},
                 integrate, side, side);
    m_current = 1 - m_current;

    // The fields, from the moved particles (which need their own bins).
    BufferHandle fieldCounts = graph.createBuffer("sph bin counts", BufferDesc{binCount * 4});
    clearBuffer(context, "sph clear bins", fieldCounts);
    dispatchGrid(context, "sph count", "fluid_sph_count.comp", {next}, {&fieldCounts}, common, side, side);
    BufferHandle fieldRanges = graph.createBuffer("sph bin ranges", BufferDesc{binCount * 8});
    BufferHandle fieldCursor = graph.createBuffer("sph bin cursor", BufferDesc{binCount * 4});
    dispatchGrid(context, "sph scan", "fluid_sph_scan.comp", {fieldCounts}, {&fieldRanges, &fieldCursor}, common, 8, 8); // one 8 x 8 group
    BufferHandle fieldIndices = graph.createBuffer("sph sorted indices", BufferDesc{particles * 4});
    dispatchGrid(context, "sph scatter", "fluid_sph_scatter.comp", {next}, {&fieldCursor, &fieldIndices}, common, side,
                 side);
    dispatchGrid(context, "sph sort bins", "fluid_sph_sort.comp", {fieldRanges}, {&fieldIndices}, common, bins, bins);
    const std::uint32_t n = m_resolution;
    const std::uint64_t cells = std::uint64_t{n} * n;
    m_outputs.density = graph.createBuffer("sph density field", BufferDesc{cells * 4});
    m_outputs.velocity = graph.createBuffer("sph velocity field", BufferDesc{cells * 8});
    const auto reach = static_cast<std::uint32_t>(std::ceil(fieldRadius() * static_cast<float>(bins)));
    dispatchGrid(context, "sph fields", "fluid_sph_fields.comp", {next, fieldRanges, fieldIndices},
                 {&m_outputs.density, &m_outputs.velocity}, FieldsPush{common, n, fieldRadius(), 0.0f, reach},
                 n, n);
    m_outputs.particles = next;
}

} // namespace fluid
