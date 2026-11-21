#ifndef PHOX_FLUID_FLUID_FEATURE_HPP
#define PHOX_FLUID_FLUID_FEATURE_HPP

#include "fluid/FluidSolver.hpp"
#include "fluid/Persistent.hpp"

#include "phox/Camera.hpp"
#include "phox/Scene.hpp"
#include "phox/vulkan/FrameFeature.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace fluid {

/// Where a point on the screen (0 to 1 across and down) looks at the fluid's surface, in the unit square
/// the grid covers: a ray from @p camera against the plane the surface rests on (its local y = 0, @p size
/// across, placed by @p model). Empty if the ray misses the surface. For pushing the fluid with the mouse.
std::optional<phox::vec2> pointOnSurface(const phox::Camera &camera, float x, float y, float aspect,
                                         const phox::mat4 &model, float size);

/// Runs a FluidSolver in every frame of a vk::Renderer and draws its density as a surface: a compute pass
/// turns the density into a heightfield (GPU-generated geometry), which the forward pass draws. With mesh
/// shaders the heights are all it makes: task and mesh shaders build the surface from them, tile by tile,
/// dropping the tiles out of view and drawing far ones with fewer points. Without, a compute pass writes
/// the triangles' vertices and an indirect command, and the surface is drawn from those.
///
/// Pipelined (the default), the surface drawn in frame n is the one computed in frame n - 1: frame n's
/// simulation and surface run on the async compute queue while the graphics queue draws the previous one,
/// so neither waits for the other. Without pipelining, frame n draws its own step's surface, and the
/// graphics queue waits for the simulation.
class FluidFeature final : public phox::vk::FrameFeature {
public:
    struct Settings {
        std::uint32_t resolution = 128; ///< cells a side
        std::uint32_t steps = 1;        ///< solver steps per frame
        bool pipelined = true;
        /// Draw the surface with task and mesh shaders when the renderer uses them, else (or when off) from
        /// vertices a compute pass writes.
        bool meshShaders = true;
        float size = 4.0f;              ///< of the surface, in world units
        float heightScale = 0.5f;       ///< world units per unit of density above the solver's rest density
        phox::mat4 model{1.0f};
        phox::Material material;
    };

    /// What readback() returns: the density and velocity after a frame's last step.
    struct Readback {
        std::uint32_t resolution = 0;
        std::vector<float> density;         ///< n * n
        std::vector<phox::vec2> velocity;   ///< n * n
        std::vector<phox::vec4> particles;  ///< particle solvers: position xy, velocity zw
    };

    FluidFeature(std::unique_ptr<FluidSolver> solver, Settings settings);
    ~FluidFeature() override;

    FluidSolver &solver() { return *m_solver; }
    const Settings &settings() const { return m_settings; }
    /// The inputs of every step of the next frames.
    void setInputs(FluidInputs inputs) { m_inputs = std::move(inputs); }
    /// The next frame copies its outputs to the CPU, available from readback() once that frame has
    /// completed (after render() returns, when rendering headless).
    void requestReadback() { m_readbackRequested = true; }
    std::optional<Readback> readback() const;

    std::vector<std::filesystem::path> shaderDirectories() const override;
    void initialize(phox::vk::Device &device, phox::vk::PipelineCache &pipelines) override;
    void shutdown() override;
    void addPasses(phox::vk::FrameFeatureContext &context, std::vector<phox::vk::GeneratedDraw> &draws) override;

private:
    void ensureVertexBuffers(phox::vk::Device &device);

    std::unique_ptr<FluidSolver> m_solver;
    Settings m_settings;
    FluidInputs m_inputs;
    // Two surfaces: pipelined, one is written while the other is drawn.
    // Mesh shader path: the heights. Vertex path: the vertices, made when first needed, and their command.
    std::array<PersistentBuffer, 2> m_heights, m_vertices, m_indirect;
    std::array<bool, 2> m_written{};
    std::uint64_t m_frames = 0;
    std::unique_ptr<phox::vk::Buffer> m_readbackDensity, m_readbackVelocity, m_readbackParticles; ///< host-visible
    bool m_readbackRequested = false;
    bool m_readbackPending = false;
    bool m_lastReadbackHadParticles = false;
};

} // namespace fluid

#endif // PHOX_FLUID_FLUID_FEATURE_HPP
