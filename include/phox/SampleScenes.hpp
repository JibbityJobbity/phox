#ifndef PHOX_SAMPLE_SCENES_HPP
#define PHOX_SAMPLE_SCENES_HPP

#include "phox/Scene.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phox {

/// A ready-made scene and how it moves over time. Used by the sample app and the golden-image tests.
struct SampleScene {
    std::string name;
    Scene scene;
    /// Poses the scene @p seconds into its animation. Deterministic: the same time gives the same pose.
    std::function<void(Scene &, float seconds)> animate;
};

/// The built-in scenes.
enum class SampleSceneId : std::uint8_t {
    Triangle,  ///< one triangle spinning about the vertical axis
    Cubes,     ///< coloured cubes on a floor, turning at different rates; one partly hides another
    Tori,      ///< tori on a floor, one cut by the edge of the view: exercises meshlet culling
    /// The material model: metal and plastic spheres from smooth to rough, base colour, metallic-
    /// roughness and normal textures, an alpha-masked screen, a blended glass sphere, emission.
    Materials,
    Field,     ///< a benchmark: 576 dense tori (37.7 million triangles), many cut by the frustum
    Crowd,     ///< a benchmark: 32,768 small spinning cubes, for the cost of each object to the renderer
};

/// Every SampleSceneId.
inline constexpr std::array kSampleScenes{SampleSceneId::Triangle, SampleSceneId::Cubes, SampleSceneId::Tori,
                                          SampleSceneId::Materials, SampleSceneId::Field, SampleSceneId::Crowd};

SampleScene makeSampleScene(SampleSceneId id);

/// The scene's name in text, e.g. "cubes": for command lines and file names.
std::string_view sampleSceneName(SampleSceneId id);
/// The scene that sampleSceneName() calls @p name, if any. For input typed by a person.
std::optional<SampleSceneId> parseSampleScene(std::string_view name);

/// @p mesh centred and scaled to fit the view, turning on a turntable.
SampleScene makeMeshViewerScene(std::shared_ptr<const Mesh> mesh, std::string name);

/// The default scene of a glTF 2.0 file (see loadGltf), centred and scaled to fit the view, turning on a
/// turntable. @throws std::runtime_error if it cannot be loaded or has nothing to draw
SampleScene makeGltfViewerScene(const std::filesystem::path &path);

} // namespace phox

#endif // PHOX_SAMPLE_SCENES_HPP
