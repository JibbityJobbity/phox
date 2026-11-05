#include "phox/SoftwareRasterizer.hpp"

#include "phox/Shading.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace phox {

namespace {

std::uint8_t toUnorm8(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// The linear colour and depth of every pixel, before tone mapping (the GPU's HDR target and depth buffer).
struct Target {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<vec3> color;
    std::vector<float> depth;
};

// Rasterizes @p draw into @p target: opaque draws replace and write depth, blended ones (@p blend) are
// composited over what is there and leave depth alone.
void drawItem(Target &target, const RenderView &view, const DrawItem &draw, bool blend) {
    const float width = static_cast<float>(target.width);
    const float height = static_cast<float>(target.height);
    const mat4 mvp = view.viewProj * draw.model;
    const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(draw.model)));
    const mat3 tangentMatrix{draw.model};
    const auto &vertices = draw.mesh->vertices();
    const auto &indices = draw.mesh->indices();
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::array<vec3, 3> screen;    // framebuffer x, y and depth
        std::array<vec3, 3> positions; // world space
        std::array<vec3, 3> normals;   // world space
        std::array<vec4, 3> tangents;  // world space, handedness in w
        std::array<vec2, 3> uvs;
        std::array<float, 3> inverseW;
        bool behind = false;
        for (std::size_t k = 0; k < 3; ++k) {
            const Vertex &v = vertices[indices[t + k]];
            const vec4 clip = mvp * vec4(v.position, 1.0f);
            if (clip.w <= 1e-5f) {
                behind = true;
                break;
            }
            const vec3 ndc = vec3(clip) / clip.w;
            screen[k] = {(ndc.x * 0.5f + 0.5f) * width, (ndc.y * 0.5f + 0.5f) * height, ndc.z};
            positions[k] = vec3(draw.model * vec4(v.position, 1.0f));
            normals[k] = normalMatrix * v.normal;
            tangents[k] = vec4(tangentMatrix * vec3(v.tangent), v.tangent.w);
            uvs[k] = v.uv;
            inverseW[k] = 1.0f / clip.w;
        }
        if (behind) {
            continue;
        }
        // Vulkan's facing rule: positive area in framebuffer coordinates is counter-clockwise.
        const float area = -0.5f * ((screen[0].x * screen[1].y - screen[1].x * screen[0].y) +
                                    (screen[1].x * screen[2].y - screen[2].x * screen[1].y) +
                                    (screen[2].x * screen[0].y - screen[0].x * screen[2].y));
        if (std::abs(area) < 1e-8f) {
            continue;
        }
        if (area < 0.0f && draw.cullBackFaces) {
            continue;
        }
        const bool frontFacing = area >= 0.0f;

        const float minX = std::min({screen[0].x, screen[1].x, screen[2].x});
        const float maxX = std::max({screen[0].x, screen[1].x, screen[2].x});
        const float minY = std::min({screen[0].y, screen[1].y, screen[2].y});
        const float maxY = std::max({screen[0].y, screen[1].y, screen[2].y});
        const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        const int x1 = std::min(static_cast<int>(target.width) - 1, static_cast<int>(std::ceil(maxX)));
        const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        const int y1 = std::min(static_cast<int>(target.height) - 1, static_cast<int>(std::ceil(maxY)));
        const auto edge = [](const vec3 &a, const vec3 &b, float px, float py) {
            return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        };
        const float total = edge(screen[0], screen[1], screen[2].x, screen[2].y);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = edge(screen[1], screen[2], px, py) / total;
                const float w1 = edge(screen[2], screen[0], px, py) / total;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                // z/w is affine in screen space, so plain barycentric interpolation is exact.
                const float z = w0 * screen[0].z + w1 * screen[1].z + w2 * screen[2].z;
                const std::size_t index = static_cast<std::size_t>(y) * target.width + static_cast<std::size_t>(x);
                if (z < 0.0f || z > 1.0f || z >= target.depth[index]) {
                    continue;
                }
                // Attributes are interpolated perspective-correctly, as the GPU does.
                const vec3 perspective{w0 * inverseW[0], w1 * inverseW[1], w2 * inverseW[2]};
                const vec3 b = perspective / (perspective.x + perspective.y + perspective.z);
                const ShadedFragment fragment =
                    shadeMaterial(*draw.material, b.x * positions[0] + b.y * positions[1] + b.z * positions[2],
                                  b.x * normals[0] + b.y * normals[1] + b.z * normals[2],
                                  b.x * tangents[0] + b.y * tangents[1] + b.z * tangents[2],
                                  b.x * uvs[0] + b.y * uvs[1] + b.z * uvs[2], frontFacing, view.cameraPosition,
                                  view.lightDirection, view.lightColor, view.ambient);
                if (fragment.discarded) {
                    continue;
                }
                if (blend) {
                    target.color[index] = glm::mix(target.color[index], fragment.color, fragment.alpha);
                } else {
                    target.depth[index] = z;
                    target.color[index] = fragment.color;
                }
            }
        }
    }
}

} // namespace

CpuImage rasterize(const RenderView &view) {
    Target target;
    target.width = view.extent.width;
    target.height = view.extent.height;
    const std::size_t pixels = static_cast<std::size_t>(target.width) * target.height;
    target.color.assign(pixels, vec3(view.clearColor));
    target.depth.assign(pixels, 1.0f);

    for (const DrawItem &draw : view.draws) drawItem(target, view, draw, false);
    for (const DrawItem &draw : view.transparent) drawItem(target, view, draw, true);

    // The GPU tonemaps the whole HDR image, background included, and writes opaque alpha.
    CpuImage image;
    image.width = target.width;
    image.height = target.height;
    image.rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        const vec3 c = applyToneMapping(target.color[i], view.toneMapping);
        image.rgba[i * 4 + 0] = toUnorm8(c.r);
        image.rgba[i * 4 + 1] = toUnorm8(c.g);
        image.rgba[i * 4 + 2] = toUnorm8(c.b);
        image.rgba[i * 4 + 3] = 255;
    }
    return image;
}

} // namespace phox
