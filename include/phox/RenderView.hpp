#ifndef PHOX_RENDER_VIEW_HPP
#define PHOX_RENDER_VIEW_HPP

#include "phox/Math.hpp"
#include "phox/Resources.hpp"
#include "phox/Scene.hpp"

#include <cstddef>
#include <vector>

namespace phox {

/// One mesh to draw with one material and transform.
struct DrawItem {
    const Mesh *mesh = nullptr;
    const Material *material = nullptr;
    mat4 model{1.0f};
    float viewDepth = 0.0f; ///< distance of the mesh's bounds centre along the camera's forward axis
};

/// What one frame draws from one camera, worked out from a Scene without any GPU involvement. Draw
/// items point into the scene, so a view must not outlive the scene it came from.
struct RenderView {
    Extent2D extent;
    mat4 view{1.0f};
    mat4 proj{1.0f};
    mat4 viewProj{1.0f};
    vec3 cameraPosition{0.0f};
    vec3 lightDirection{0.0f, 0.0f, 1.0f}; ///< unit vector towards the light
    vec3 lightColor{1.0f};
    float ambient = 0.25f;
    vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    ToneMapping toneMapping;
    std::vector<DrawItem> draws; ///< visible items, nearest first
    std::size_t culled = 0;      ///< objects skipped because they are outside the view frustum
};

/// Builds the view for @p scene rendered at @p extent: camera matrices, the light, and the objects inside
/// the view frustum sorted front to back. Objects without a mesh, or with an empty one, are skipped.
RenderView extractView(const Scene &scene, Extent2D extent);

/// Whether a box, transformed by @p mvp into clip space, lies entirely outside one of the frustum planes.
bool outsideFrustum(const mat4 &mvp, const Bounds &bounds);

} // namespace phox

#endif // PHOX_RENDER_VIEW_HPP
