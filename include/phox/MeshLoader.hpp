#ifndef PHOX_MESH_LOADER_HPP
#define PHOX_MESH_LOADER_HPP

#include "phox/Scene.hpp"

#include <filesystem>

namespace phox {

/// Loads every triangle mesh in a model file into one Mesh, with node transforms applied and missing
/// normals generated. The formats are the ones the build's assimp includes (OBJ by default).
/// @throws std::runtime_error if the file cannot be read or contains no triangles
Mesh loadMesh(const std::filesystem::path &path);

} // namespace phox

#endif // PHOX_MESH_LOADER_HPP
