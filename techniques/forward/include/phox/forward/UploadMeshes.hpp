#ifndef PHOX_FORWARD_UPLOAD_MESHES_HPP
#define PHOX_FORWARD_UPLOAD_MESHES_HPP

#include "phox/graph/Component.hpp"
#include "phox/scene/Camera.hpp"
#include "phox/scene/Scene.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace phox::forward {

/// One interleaved vertex as the forward pipeline consumes it (32 bytes).
struct Vertex {
    float position[3];
    float normal[3];
    float texCoord[2];
};
static_assert(sizeof(Vertex) == 32);

/// Flattens every triangle of a scene into one vertex stream and uploads it, together with the camera
/// matrix, through host-written buffers. Normals are (0, 0, 1) and texture coordinates (0, 0). The
/// albedo comes from the first object. An empty scene yields `vertexCount == 0` and invalid buffers,
/// and adds no pass.
class UploadMeshes {
  public:
    struct In {
        const scene::Scene *scene = nullptr;
        const scene::Camera *camera = nullptr;
    };
    struct Out {
        graph::GraphBuffer vertices;
        uint32_t vertexCount = 0;
        graph::GraphBuffer camera;
        std::array<float, 4> albedo{1.0f, 0.2f, 0.4f, 1.0f};
    };

    Out setup(graph::GraphBuilder &builder, const In &in);
    std::string_view name() const { return "UploadMeshes"; }
};

static_assert(graph::Component<UploadMeshes>);

} // namespace phox::forward

#endif // PHOX_FORWARD_UPLOAD_MESHES_HPP
