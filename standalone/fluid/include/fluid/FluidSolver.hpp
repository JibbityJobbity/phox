#ifndef PHOX_FLUID_FLUID_SOLVER_HPP
#define PHOX_FLUID_FLUID_SOLVER_HPP

#include "phox/Math.hpp"
#include "phox/RenderGraph.hpp"
#include "phox/vulkan/FrameFeature.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

/// The fluid sample: 2D fluid solvers that run as async compute passes in Phox's render graph, behind
/// one interface, and a frame feature that draws the result as a surface.
namespace fluid {

/// A push on the fluid, e.g. from an emitter or the mouse.
struct Splat {
    phox::vec2 position{0.5f}; ///< in the unit square the grid covers
    phox::vec2 force{0.0f};    ///< domain units per second squared (each solver scales it to its own units)
    float radius = 0.05f;      ///< of the Gaussian, in domain units
    float amount = 0.0f;       ///< dye added per second at the centre (solvers that carry dye)
};

/// The same for every solver.
struct FluidInputs {
    float dt = 1.0f / 60.0f;   ///< seconds per step
    std::vector<Splat> splats; ///< the first kMaxSplats are used
};
inline constexpr std::size_t kMaxSplats = 3; // shaders/fluid_common.glsl

/// What a step leaves in the graph, n x n cells each (row by row, y up), for the surface and for tests.
struct FluidOutputs {
    phox::BufferHandle density;  ///< float per cell: the dye (stable fluids) or the density (lattice Boltzmann, SPH)
    phox::BufferHandle velocity; ///< vec2 per cell, domain units per second (lattice units for LBM)
    /// Particle solvers: vec4 per particle, position in the unit square (xy) and velocity (zw).
    std::optional<phox::BufferHandle> particles;
};

/// A 2D fluid on an n x n grid over the unit square, with solid walls. Its state lives in buffers it owns
/// across frames; each frame it imports them into the graph (beginFrame) and adds the passes of one or
/// more steps, all on the async compute queue where there is one.
class FluidSolver {
public:
    virtual ~FluidSolver() = default;
    virtual std::string_view name() const = 0;
    std::uint32_t resolution() const { return m_resolution; }

    /// Creates the state for @p resolution cells a side (a multiple of 8 is best). The fluid starts at rest.
    virtual void initialize(phox::vk::Device &device, std::uint32_t resolution) = 0;
    virtual void shutdown() = 0;
    /// The next frame starts from rest again.
    virtual void reset() = 0;
    /// The density a fluid at rest has (the surface's base height).
    virtual float restDensity() const = 0;
    /// Particle solvers: how many particles FluidOutputs::particles holds.
    virtual std::uint32_t particleCount() const { return 0; }

    /// Imports this frame's state. Call before step().
    virtual void beginFrame(phox::vk::FrameFeatureContext &context) = 0;
    /// Adds one step's passes.
    virtual void step(phox::vk::FrameFeatureContext &context, const FluidInputs &inputs) = 0;
    /// After the frame's steps.
    virtual FluidOutputs outputs() const = 0;

protected:
    std::uint32_t m_resolution = 0;
};

} // namespace fluid

#endif // PHOX_FLUID_FLUID_SOLVER_HPP
