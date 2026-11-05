#ifndef PHOX_GLTF_LOADER_HPP
#define PHOX_GLTF_LOADER_HPP

#include "phox/Math.hpp"
#include "phox/Scene.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace phox {

/// What loadGltf added to a scene.
struct GltfImport {
    std::vector<ObjectId> objects; ///< one per mesh primitive per node that uses the mesh
    Bounds bounds;                 ///< of every object, in world space (after @p root)
    std::size_t meshes = 0;        ///< distinct Phox meshes (glTF primitives) created
    std::size_t materials = 0;
    std::size_t textures = 0;
    std::vector<std::string> warnings; ///< features that were ignored, e.g. extensions Phox does not support
};

/// Loads the default scene of a glTF 2.0 file (.gltf with external, embedded or data-URI buffers and
/// images, or binary .glb) into @p scene, each object placed by its node's world transform times @p root.
/// Supported: triangle primitives (lists, strips and fans; indexed or not) with POSITION, NORMAL (flat
/// normals when missing, as the spec says), TEXCOORD_0 and TANGENT (generated when missing), the
/// metallic-roughness material model with its five textures, alpha modes, double-sided materials,
/// samplers, KHR_materials_emissive_strength. PNG and JPEG images. Primitives that share a glTF mesh and
/// accessors share their Phox Mesh, and textures their Texture. Not supported (listed in warnings):
/// skins, morph targets, animations, cameras, lights, texture coordinates other than TEXCOORD_0,
/// KHR_texture_transform and other extensions; points and lines are skipped.
/// @throws std::runtime_error if the file cannot be read or parsed
GltfImport loadGltf(const std::filesystem::path &path, Scene &scene, const mat4 &root = mat4(1.0f));

} // namespace phox

#endif // PHOX_GLTF_LOADER_HPP
