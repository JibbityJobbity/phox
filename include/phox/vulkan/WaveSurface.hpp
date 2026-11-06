#ifndef PHOX_VULKAN_WAVE_SURFACE_HPP
#define PHOX_VULKAN_WAVE_SURFACE_HPP

#include "phox/Scene.hpp"
#include "phox/vulkan/FrameFeature.hpp"

#include <cstdint>

namespace phox::vk {

/// A sample generator of GPU geometry: a square of waves in the XZ plane, built every frame by a compute
/// pass on the async compute queue (shaders/wavesurface.comp), which also writes the indirect draw. It
/// shows the shape of a FrameFeature, and with referenceMesh() lets tests check generated draws against
/// the same mesh drawn from the scene.
class WaveSurface final : public FrameFeature {
public:
    struct Settings {
        std::uint32_t resolution = 64; ///< cells along each side
        float size = 4.0f;             ///< side length, centred on the origin
        float amplitude = 0.2f;
        float wavenumber = 3.0f;       ///< radians per unit
        mat4 model{1.0f};
        Material material;
        bool async = true;             ///< run the compute pass with PassBuilder::async()
    };

    explicit WaveSurface(Settings settings) : m_settings(settings) {}

    /// The animation time of the next frames' surface.
    void setTime(float time) { m_time = time; }
    const Settings &settings() const { return m_settings; }

    /// The surface at @p time as a CPU mesh, triangle for triangle as the shader writes it.
    static Mesh referenceMesh(const Settings &settings, float time);

    void addPasses(FrameFeatureContext &context, std::vector<GeneratedDraw> &draws) override;

private:
    Settings m_settings;
    float m_time = 0.0f;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_WAVE_SURFACE_HPP
