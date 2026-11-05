#ifndef PHOX_SAMPLE_SCENES_HPP
#define PHOX_SAMPLE_SCENES_HPP

#include "phox/Scene.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phox {

/// A ready-made scene and how it moves over time. Used by the sample app and the golden-image tests.
struct SampleScene {
    std::string name;
    Scene scene;
    /// Poses the scene @p seconds into its animation. Deterministic: the same time gives the same pose.
    std::function<void(Scene &, float seconds)> animate;
};

/// "triangle": one triangle spinning about the vertical axis.
/// "cubes":    coloured cubes on a floor, turning at different rates; one partly hides another.
/// "tori":     tori on a floor, one cut by the edge of the view: exercises meshlet culling.
/// "materials": the material model: metal and plastic spheres from smooth to rough, base colour, metallic-
///             roughness and normal textures, an alpha-masked screen, a blended glass sphere, emission.
/// "field":    a benchmark: 576 dense tori (37.7 million triangles), many cut by the frustum.
std::vector<std::string> sampleSceneNames();

/// @throws std::invalid_argument for an unknown name
SampleScene makeSampleScene(const std::string &name);

/// @p mesh centred and scaled to fit the view, turning on a turntable.
SampleScene makeMeshViewerScene(std::shared_ptr<const Mesh> mesh, std::string name);

/// The default scene of a glTF 2.0 file (see loadGltf), centred and scaled to fit the view, turning on a
/// turntable. @throws std::runtime_error if it cannot be loaded or has nothing to draw
SampleScene makeGltfViewerScene(const std::filesystem::path &path);

} // namespace phox

#endif // PHOX_SAMPLE_SCENES_HPP
