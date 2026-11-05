#include "phox/RenderView.hpp"

#include "phox/Meshlets.hpp"

#include <algorithm>
#include <array>

namespace phox {

bool outsideFrustum(const mat4 &mvp, const Bounds &bounds) {
    std::array<vec4, 8> corners;
    for (int i = 0; i < 8; ++i) {
        const vec3 corner{(i & 1) ? bounds.max.x : bounds.min.x, (i & 2) ? bounds.max.y : bounds.min.y,
                          (i & 4) ? bounds.max.z : bounds.min.z};
        corners[static_cast<std::size_t>(i)] = mvp * vec4(corner, 1.0f);
    }
    // Clip space (Vulkan): -w <= x, y <= w and 0 <= z <= w.
    const auto allOutside = [&](auto outside) {
        return std::all_of(corners.begin(), corners.end(), outside);
    };
    return allOutside([](const vec4 &c) { return c.x < -c.w; }) || allOutside([](const vec4 &c) { return c.x > c.w; }) ||
           allOutside([](const vec4 &c) { return c.y < -c.w; }) || allOutside([](const vec4 &c) { return c.y > c.w; }) ||
           allOutside([](const vec4 &c) { return c.z < 0.0f; }) || allOutside([](const vec4 &c) { return c.z > c.w; });
}

RenderView extractView(const Scene &scene, Extent2D extent) {
    RenderView view;
    view.extent = extent;
    const Camera &camera = scene.camera();
    const float aspect = extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
    view.view = camera.view();
    view.proj = camera.projection(aspect);
    view.viewProj = view.proj * view.view;
    view.frustumPlanes = frustumPlanes(view.viewProj);
    view.cameraPosition = camera.position;
    const DirectionalLight &light = scene.light();
    view.lightDirection = glm::length(light.direction) > 0.0f ? -glm::normalize(light.direction) : vec3(0.0f, 0.0f, 1.0f);
    view.lightColor = light.color;
    view.ambient = light.ambient;
    view.clearColor = scene.clearColor;
    view.toneMapping = scene.toneMapping;

    const vec3 forward = camera.forward();
    for (const Object &object : scene.objects()) {
        const Mesh *mesh = object.mesh().get();
        if (mesh == nullptr || mesh->empty()) {
            continue;
        }
        const mat4 model = object.transform().matrix();
        if (outsideFrustum(view.viewProj * model, mesh->bounds())) {
            ++view.culled;
            continue;
        }
        const vec3 center = vec3(model * vec4(mesh->bounds().center(), 1.0f));
        const bool cullBackFaces = !object.material().doubleSided && glm::determinant(mat3(model)) > 0.0f;
        view.draws.push_back(
            {object.id(), mesh, &object.material(), model, glm::dot(center - camera.position, forward), cullBackFaces});
    }
    // Front to back, so depth testing rejects hidden fragments early. Stable: ties keep scene order.
    std::stable_sort(view.draws.begin(), view.draws.end(),
                     [](const DrawItem &a, const DrawItem &b) { return a.viewDepth < b.viewDepth; });
    return view;
}

} // namespace phox
