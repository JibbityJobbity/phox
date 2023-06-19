#include "Model.hpp"

#include <fstream>
#include <iostream>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

namespace phox {

Model Model::FromObj(Device &device, std::string &path) {
    Model model;
    model.m_device = &device;

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path, aiProcess_Triangulate);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
        !scene->mRootNode) {
        throw std::runtime_error(std::string("ERROR::ASSIMP::") +
                                 std::string(importer.GetErrorString()));
    }

    if (scene->mNumMeshes != 1) {
        throw std::runtime_error("ERROR::ASSIMP::" + path +
                                 " must contain only one mesh");
    }

    auto *mesh = scene->mMeshes[0];
    model.m_vertices.reserve(mesh->mNumVertices);
    for (size_t i = 0; i < mesh->mNumVertices; i++) {
        model.m_vertices.emplace_back(
            Vertex{glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y,
                             mesh->mVertices[i].z),
                   glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y,
                             mesh->mNormals[i].z),
                   glm::vec2(mesh->mTextureCoords[0][i].x,
                             mesh->mTextureCoords[0][i].y)});
    }
    for (size_t i = 0; i < mesh->mFaces->mNumIndices; i++) {
        model.m_indices.push_back(mesh->mFaces->mIndices[i]);
    }

    Buffer vertexBuffer = device.allocateBuffer(
        model.m_vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryLocation::Host);
    Buffer indexBuffer = device.allocateBuffer(
        model.m_indices.size() * sizeof(model.m_indices[0]),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, MemoryLocation::Host);

    return model;
}

} // namespace phox
