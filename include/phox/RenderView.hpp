#ifndef PHOX_RENDER_VIEW_HPP
#define PHOX_RENDER_VIEW_HPP

#include "phox/Math.hpp"
#include "phox/Resources.hpp"
#include "phox/Scene.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace phox {

/// One mesh to draw with one material and transform.
struct DrawItem {
    std::uint64_t objectId = 0; ///< Object::id() of the object drawn
    /// ObjectId::index of the object in its scene: dense and stable while the object exists, so renderers
    /// can keep per-object state in arrays (another object may have it later; objectId tells them apart).
    std::uint32_t sceneIndex = 0;
    const Mesh *mesh = nullptr;
    const Material *material = nullptr;
    mat4 model{1.0f};
    float viewDepth = 0.0f; ///< distance of the mesh's bounds centre along the camera's forward axis
    /// Back faces are culled: the material is single-sided and the transform does not mirror the mesh
    /// (which would turn its front faces into back faces). Otherwise both faces are drawn.
    bool cullBackFaces = false;
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
    std::array<vec4, 6> frustumPlanes{}; ///< world space, from viewProj (see phox::frustumPlanes)
    std::vector<DrawItem> draws; ///< visible opaque and alpha-masked items, nearest first
    /// visible items with AlphaMode::Blend, farthest first: drawn after `draws`, blended, without depth writes
    std::vector<DrawItem> transparent;
    std::size_t culled = 0; ///< objects skipped because they are outside the view frustum

    /// draws.size() + transparent.size(). Renderers number the items in that order (draws first), e.g. to
    /// index a per-frame material table.
    std::size_t itemCount() const { return draws.size() + transparent.size(); }
};

/// Builds the view for @p scene rendered at @p extent: camera matrices, the light, and the objects inside
/// the view frustum sorted front to back. Objects without a mesh, or with an empty one, are skipped.
/// With @p cull false, every object is listed and draws keep scene order (renderers that cull on the GPU
/// want them all); transparent items are still sorted back to front, as blending needs.
RenderView extractView(const Scene &scene, Extent2D extent, bool cull = true);

/// Whether a box, transformed by @p mvp into clip space, lies entirely outside one of the frustum planes.
bool outsideFrustum(const mat4 &mvp, const Bounds &bounds);

} // namespace phox

#endif // PHOX_RENDER_VIEW_HPP
