#ifndef PHOX_FLUID_SMOOTHED_PARTICLES_HPP
#define PHOX_FLUID_SMOOTHED_PARTICLES_HPP

#include "fluid/FluidSolver.hpp"
#include "fluid/Persistent.hpp"

#include <array>

namespace fluid {

/// Weakly compressible smoothed particle hydrodynamics: particles filling the tank, each step finding
/// its neighbours on a grid of bins (count, scan, scatter, then each bin sorted by index so every sum runs
/// in a reproducible order), summing density and pressure, then pressure, viscosity and the splats
/// accelerating every particle (symplectic Euler, damped walls). The fields are gathered from the
/// particles onto the grid with a wider kernel: density relative to the initial lattice's at the same
/// point (1 at rest, edges included), and kernel-weighted velocity.
/// No floating-point atomics: everything is a gather.
class SmoothedParticles final : public FluidSolver {
public:
    struct Settings {
        std::uint32_t side = 128;     ///< particles a side, side * side in all
        float smoothing = 3.0f;       ///< the smoothing radius, in initial particle spacings
        float fieldSmoothing = 2.0f;  ///< the fields' kernel radius, in smoothing radii (smoother than the physics)
        float soundSpeed = 2.5f;      ///< domain units per second; sets the stiffness
        float viscosity = 0.002f;
        /// Splat::force to acceleration. Weakly compressible SPH must stay well below its speed of sound,
        /// and nothing else (no projection, as in stable fluids) caps how fast stirring spins the tank up.
        float forceScale = 0.3f;
        float drag = 1.0f;            ///< velocity lost per second, so a stirred tank reaches a steady speed
        float wallDamping = 0.5f;
        /// Walls push back on particles closer than half a spacing (where none starts), so interior pressure
        /// cannot pack them into a row against the wall. In units of c^2 / spacing.
        float wallStiffness = 0.5f;
    };
    SmoothedParticles() = default;
    explicit SmoothedParticles(Settings settings) : m_settings(settings) {}

    std::string_view name() const override { return "smoothed particle hydrodynamics"; }
    void initialize(phox::vk::Device &device, std::uint32_t resolution) override;
    void shutdown() override;
    void reset() override { m_valid = false; }
    float restDensity() const override { return 1.0f; } // the fields' density is relative to rest
    std::uint32_t particleCount() const override { return m_settings.side * m_settings.side; }
    void beginFrame(phox::vk::FrameFeatureContext &context) override;
    void step(phox::vk::FrameFeatureContext &context, const FluidInputs &inputs) override;
    FluidOutputs outputs() const override { return m_outputs; }

    /// The 2D poly6 kernel, as the shaders compute it, and the density of the initial lattice with it.
    static float poly6(float r2, float h);
    float smoothingRadius() const { return m_settings.smoothing / static_cast<float>(m_settings.side); }
    float fieldRadius() const { return m_settings.fieldSmoothing * smoothingRadius(); }
    float particleMass() const { return 1.0f / static_cast<float>(particleCount()); }
    float latticeDensity() const;
    std::uint32_t binsPerSide() const;

private:
    Settings m_settings;
    std::array<PersistentBuffer, 2> m_stateBuffers;
    std::array<phox::BufferHandle, 2> m_state;
    int m_current = 0;
    FluidOutputs m_outputs;
    bool m_valid = false;
};

} // namespace fluid

#endif // PHOX_FLUID_SMOOTHED_PARTICLES_HPP
