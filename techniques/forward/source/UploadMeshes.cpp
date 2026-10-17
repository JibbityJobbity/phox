#include "phox/forward/UploadMeshes.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace phox::forward {

UploadMeshes::Out UploadMeshes::setup(graph::GraphBuilder &builder, const In &in) {
    if (in.scene == nullptr || in.camera == nullptr) {
        throw std::invalid_argument("UploadMeshes: both a scene and a camera are required");
    }

    // Copy everything the execute callback needs now, so the scene can change before the graph runs.
    auto vertices = std::make_shared<std::vector<Vertex>>();
    for (const auto &object : in.scene->objects()) {
        for (const scene::Triangle &triangle : object->mesh().triangles()) {
            for (const scene::MeshVertex *v : {&triangle.a, &triangle.b, &triangle.c}) {
                vertices->push_back({{v->x, v->y, v->z}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
            }
        }
    }

    Out out;
    if (vertices->empty()) {
        return out;
    }
    out.vertexCount = static_cast<uint32_t>(vertices->size());
    out.albedo = in.scene->objects().front()->material().albedo;

    const std::array<float, 16> viewProjection = in.camera->viewProjection();
    const graph::GraphBuffer vertexBuffer =
        builder.createBuffer({vertices->size() * sizeof(Vertex), rhi::BufferUsage{}, rhi::MemoryKind::Upload, "vertices"});
    const graph::GraphBuffer cameraBuffer =
        builder.createBuffer({sizeof(viewProjection), rhi::BufferUsage{}, rhi::MemoryKind::Upload, "camera"});
    out.vertices = vertexBuffer;
    out.camera = cameraBuffer;

    builder.addPass(
        "upload meshes",
        [=](graph::PassBuilder &pass) {
            pass.write(vertexBuffer, graph::BufferAccess::HostWrite);
            pass.write(cameraBuffer, graph::BufferAccess::HostWrite);
        },
        [=](graph::PassContext &context) {
            const auto vertexBytes = context.device.map(context.buffer(vertexBuffer));
            std::memcpy(vertexBytes.data(), vertices->data(), vertices->size() * sizeof(Vertex));
            const auto cameraBytes = context.device.map(context.buffer(cameraBuffer));
            std::memcpy(cameraBytes.data(), viewProjection.data(), sizeof(viewProjection));
        });
    return out;
}

} // namespace phox::forward
