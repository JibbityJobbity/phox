#ifndef PHOX_FLUID_STABLE_FLUIDS_HPP
#define PHOX_FLUID_STABLE_FLUIDS_HPP

#include "fluid/FluidSolver.hpp"
#include "fluid/Persistent.hpp"

#include <array>

namespace fluid {

/// Incompressible Navier-Stokes after Stam's "Stable Fluids": per step, splat forces and dye, then
/// semi-Lagrangian advection of velocity and dye, then a pressure projection (divergence, Jacobi
/// iterations warm-started from the last step's pressure, gradient subtraction). Unconditionally stable;
/// viscosity is only the numerical diffusion of advection, plus optional dissipation.
class StableFluids final : public FluidSolver {
public:
    struct Settings {
        std::uint32_t iterations = 40;    ///< Jacobi iterations per step (0: no projection, for tests)
        float velocityDissipation = 0.1f; ///< per second
        float dyeDissipation = 0.15f;     ///< per second
    };
    StableFluids() = default;
    explicit StableFluids(Settings settings) : m_settings(settings) {}

    std::string_view name() const override { return "stable fluids"; }
    void initialize(phox::vk::Device &device, std::uint32_t resolution) override;
    void shutdown() override;
    void reset() override { m_valid = false; }
    float restDensity() const override { return 0.0f; }
    void beginFrame(phox::vk::FrameFeatureContext &context) override;
    void step(phox::vk::FrameFeatureContext &context, const FluidInputs &inputs) override;
    FluidOutputs outputs() const override {
        return {m_dye[m_currentDye], m_velocity[m_currentVelocity], std::nullopt};
    }

private:
    Settings m_settings;
    // Ping-pong pairs; m_current* says which holds the latest contents (in buffers and graph handles alike).
    std::array<PersistentBuffer, 2> m_velocityBuffers, m_dyeBuffers, m_pressureBuffers;
    std::array<phox::BufferHandle, 2> m_velocity, m_dye, m_pressure;
    int m_currentVelocity = 0, m_currentDye = 0, m_currentPressure = 0;
    bool m_valid = false; ///< the buffers hold a fluid (else the frame starts by clearing them)
};

} // namespace fluid

#endif // PHOX_FLUID_STABLE_FLUIDS_HPP
