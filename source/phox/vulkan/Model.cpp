#include "phox/vulkan/Model.hpp"

#include <fstream>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

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
    const bool hasNormals = mesh->HasNormals();
    const bool hasUvs = mesh->HasTextureCoords(0);
    for (size_t i = 0; i < mesh->mNumVertices; i++) {
        const aiVector3D &p = mesh->mVertices[i];
        const glm::vec3 normal = hasNormals
            ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
            : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec2 uv = hasUvs
            ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
            : glm::vec2(0.0f);
        model.m_vertices.emplace_back(Vertex{glm::vec3(p.x, p.y, p.z), normal, uv});
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
        const aiFace &face = mesh->mFaces[f];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            model.m_indices.push_back(face.mIndices[j]);
        }
    }

    if (model.m_vertices.empty() || model.m_indices.empty()) {
        throw std::runtime_error("ERROR::ASSIMP::" + path + " contains no geometry");
    }

    const size_t vertexBytes = model.m_vertices.size() * sizeof(Vertex);
    const size_t indexBytes = model.m_indices.size() * sizeof(model.m_indices[0]);
    model.m_vertexBuffer = device.allocateBuffer(vertexBytes, MemoryLocation::Host);
    model.m_indexBuffer = device.allocateBuffer(indexBytes, MemoryLocation::Host);
    std::memcpy(model.m_vertexBuffer->map(), model.m_vertices.data(), vertexBytes);
    model.m_vertexBuffer->unmap();
    std::memcpy(model.m_indexBuffer->map(), model.m_indices.data(), indexBytes);
    model.m_indexBuffer->unmap();

    return model;
}

} // namespace phox
