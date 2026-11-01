#include "phox/SoftwareRasterizer.hpp"

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

} // namespace

CpuImage rasterize(const RenderView &view) {
    const std::uint32_t width = view.extent.width;
    const std::uint32_t height = view.extent.height;
    CpuImage image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    // The GPU tonemaps the whole HDR image, background included, and writes opaque alpha.
    const vec3 background = applyToneMapping(vec3(view.clearColor), view.toneMapping);
    const std::array<std::uint8_t, 4> clear{toUnorm8(background.r), toUnorm8(background.g), toUnorm8(background.b), 255};
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        std::copy(clear.begin(), clear.end(), image.rgba.begin() + static_cast<std::ptrdiff_t>(i));
    }
    std::vector<float> depth(static_cast<std::size_t>(width) * height, 1.0f);

    for (const DrawItem &draw : view.draws) {
        const mat4 mvp = view.viewProj * draw.model;
        const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(draw.model)));
        const auto &vertices = draw.mesh->vertices();
        const auto &indices = draw.mesh->indices();
        for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
            std::array<vec3, 3> screen;  // framebuffer x, y and depth
            std::array<vec3, 3> normals; // world space
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
                screen[k] = {(ndc.x * 0.5f + 0.5f) * float(width), (ndc.y * 0.5f + 0.5f) * float(height), ndc.z};
                normals[k] = normalMatrix * v.normal;
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
            // Back faces are lit from behind, like the GPU's two-sided shading.
            const float facing = area < 0.0f ? -1.0f : 1.0f;

            const float minX = std::min({screen[0].x, screen[1].x, screen[2].x});
            const float maxX = std::max({screen[0].x, screen[1].x, screen[2].x});
            const float minY = std::min({screen[0].y, screen[1].y, screen[2].y});
            const float maxY = std::max({screen[0].y, screen[1].y, screen[2].y});
            const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
            const int x1 = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(maxX)));
            const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
            const int y1 = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(maxY)));
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
                    const std::size_t index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (z < 0.0f || z > 1.0f || z >= depth[index]) {
                        continue;
                    }
                    depth[index] = z;
                    // Normals are interpolated perspective-correctly and renormalised, as the GPU does.
                    const vec3 perspective{w0 * inverseW[0], w1 * inverseW[1], w2 * inverseW[2]};
                    const vec3 weights = perspective / (perspective.x + perspective.y + perspective.z);
                    const vec3 normal =
                        facing * glm::normalize(weights.x * normals[0] + weights.y * normals[1] + weights.z * normals[2]);
                    const float diffuse = std::max(glm::dot(normal, view.lightDirection), 0.0f);
                    const vec3 lit = applyToneMapping(
                        vec3(draw.material->albedo) * view.lightColor * (view.ambient + (1.0f - view.ambient) * diffuse),
                        view.toneMapping);
                    const std::array<std::uint8_t, 4> color{toUnorm8(lit.r), toUnorm8(lit.g), toUnorm8(lit.b), 255};
                    std::copy(color.begin(), color.end(), image.rgba.begin() + static_cast<std::ptrdiff_t>(index * 4));
                }
            }
        }
    }
    return image;
}

} // namespace phox
