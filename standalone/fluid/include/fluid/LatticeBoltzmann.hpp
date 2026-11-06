#ifndef PHOX_FLUID_LATTICE_BOLTZMANN_HPP
#define PHOX_FLUID_LATTICE_BOLTZMANN_HPP

#include "fluid/FluidSolver.hpp"
#include "fluid/Persistent.hpp"

#include <array>

namespace fluid {

/// The lattice Boltzmann method on a D2Q9 lattice: per step, the nine populations of every cell stream to
/// their neighbours (bouncing back off the walls) and relax towards equilibrium (BGK), pushed by the splats
/// as a body force. Weakly compressible: pressure waves travel at the lattice's speed of sound, and the
/// density shows them. Everything is in lattice units; a step is one lattice time step.
class LatticeBoltzmann final : public FluidSolver {
public:
    struct Settings {
        float tau = 0.56f;         ///< relaxation time, > 0.5; viscosity (tau - 0.5) / 3
        float forceScale = 2e-4f;  ///< Splat::force to lattice units
    };
    LatticeBoltzmann() = default;
    explicit LatticeBoltzmann(Settings settings) : m_settings(settings) {}

    std::string_view name() const override { return "lattice Boltzmann"; }
    void initialize(phox::vk::Device &device, std::uint32_t resolution) override;
    void shutdown() override;
    void reset() override { m_valid = false; }
    float restDensity() const override { return 1.0f; }
    void beginFrame(phox::vk::FrameFeatureContext &context) override;
    void step(phox::vk::FrameFeatureContext &context, const FluidInputs &inputs) override;
    FluidOutputs outputs() const override { return m_outputs; }

private:
    Settings m_settings;
    std::array<PersistentBuffer, 2> m_populationBuffers;
    std::array<phox::BufferHandle, 2> m_populations;
    int m_current = 0;
    FluidOutputs m_outputs;
    bool m_valid = false;
};

} // namespace fluid

#endif // PHOX_FLUID_LATTICE_BOLTZMANN_HPP
