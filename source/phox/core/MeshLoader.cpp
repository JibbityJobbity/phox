#include "phox/MeshLoader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <stdexcept>
#include <string>

namespace phox {

Mesh loadMesh(const std::filesystem::path &path) {
    Assimp::Importer importer;
    // Pre-transforming bakes the node hierarchy into the vertices, so the meshes can simply be appended.
    const aiScene *scene = importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                                aiProcess_JoinIdenticalVertices |
                                                                aiProcess_PreTransformVertices | aiProcess_SortByPType);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        throw std::runtime_error("cannot load " + path.string() + ": " + importer.GetErrorString());
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh *mesh = scene->mMeshes[m];
        if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
            continue; // points and lines
        }
        const auto base = static_cast<std::uint32_t>(vertices.size());
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v;
            v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            if (mesh->HasNormals()) {
                v.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
            }
            if (mesh->HasTextureCoords(0)) {
                v.uv = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
            }
            vertices.push_back(v);
        }
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace &face = mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue;
            }
            for (unsigned k = 0; k < 3; ++k) indices.push_back(base + face.mIndices[k]);
        }
    }
    if (indices.empty()) {
        throw std::runtime_error(path.string() + " contains no triangles");
    }
    return Mesh(std::move(vertices), std::move(indices));
}

} // namespace phox
