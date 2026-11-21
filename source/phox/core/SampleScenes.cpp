#include "phox/SampleScenes.hpp"

#include "phox/Color.hpp"
#include "phox/GltfLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace phox {

namespace {

quat spin(float radians, const vec3 &axis) { return glm::angleAxis(radians, glm::normalize(axis)); }

// A fairly rough dielectric in a colour picked in sRGB: flat colours with a soft highlight.
Material paint(float r, float g, float b, bool doubleSided = true) {
    return Material{.albedo = srgb(r, g, b), .roughness = 0.6f, .doubleSided = doubleSided};
}

SampleScene triangle() {
    SampleScene sample;
    Scene &scene = sample.scene;
    const ObjectId tri =
        scene.addObject({.mesh = std::make_shared<Mesh>(Mesh::triangle(1.0f)), .material = paint(1.0f, 0.2f, 0.4f)});
    scene.camera().position = {0.0f, 0.0f, 3.0f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    scene.light().direction = {-0.4f, -0.6f, -1.0f};
    sample.animate = [tri](Scene &s, float t) { s.at(tri).transform().rotation = spin(1.2f * t, {0, 1, 0}); };
    return sample;
}

SampleScene cubes() {
    SampleScene sample;
    Scene &scene = sample.scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(1.0f));
    scene.addObject({.mesh = std::make_shared<Mesh>(Mesh::plane(8.0f)),
                     .material = paint(0.55f, 0.55f, 0.6f),
                     .transform = {.position = {0.0f, -0.5f, 0.0f}}});
    const auto [red, green, blue, yellow, purple] = scene.addObjects({
        {.mesh = cube, .material = paint(0.9f, 0.25f, 0.2f)},
        {.mesh = cube, .material = paint(0.3f, 0.75f, 0.35f), .transform = {.position = {-1.6f, 0.0f, -0.8f}}},
        {.mesh = cube, .material = paint(0.25f, 0.45f, 0.9f), .transform = {.position = {1.6f, 0.0f, -0.8f}}},
        {.mesh = cube,
         .material = paint(0.9f, 0.8f, 0.3f),
         .transform = {.position = {0.0f, 0.25f, -2.2f}, .scale = {1.0f, 1.5f, 1.0f}}},
        // In front of the red cube and overlapping it on screen, so depth testing has something to do.
        {.mesh = cube,
         .material = paint(0.6f, 0.35f, 0.85f),
         .transform = {.position = {0.55f, -0.25f, 1.0f}, .scale = vec3(0.5f)}},
    });
    static_cast<void>(yellow); // stands still
    scene.camera().position = {0.0f, 2.2f, 5.0f};
    scene.camera().lookAt({0.0f, 0.0f, -0.3f});
    scene.light().direction = {-0.5f, -1.0f, -0.35f};
    sample.animate = [red, green, blue, purple](Scene &s, float t) {
        s.at(red).transform().rotation = spin(0.8f * t + 0.4f, {0, 1, 0});
        s.at(green).transform().rotation = spin(-0.6f * t, {0, 1, 0});
        s.at(blue).transform().rotation = spin(0.5f * t + 0.3f, {0.3f, 1, 0});
        s.at(purple).transform().rotation = spin(1.5f * t + 0.6f, {1, 1, 0});
    };
    return sample;
}

// Tori on a floor, one of them cut by the left edge of the view and one close enough to fill a good part
// of it. Each torus is a few dozen meshlets, about half of them facing away, so the mesh shading path's
// frustum and cone culling both have work to do.
SampleScene tori() {
    SampleScene sample;
    Scene &scene = sample.scene;
    auto torus = std::make_shared<Mesh>(Mesh::torus(0.6f, 0.22f, 64, 32));
    scene.addObject({.mesh = std::make_shared<Mesh>(Mesh::plane(10.0f)),
                     .material = paint(0.55f, 0.55f, 0.6f, false),
                     .transform = {.position = {0.0f, -0.5f, -1.0f}}});
    const auto [near, edge, back, left] = scene.addObjects({
        {.mesh = torus, .material = paint(0.95f, 0.5f, 0.15f, false), .transform = {.position = {0.5f, 0.1f, 1.6f}}},
        {.mesh = torus, .material = paint(0.2f, 0.7f, 0.7f, false), .transform = {.position = {-2.6f, 0.2f, 0.0f}}},
        {.mesh = torus,
         .material = paint(0.6f, 0.4f, 0.9f, false),
         .transform = {.position = {0.9f, 0.4f, -2.2f}, .scale = vec3(1.4f)}},
        {.mesh = torus, .material = paint(0.55f, 0.85f, 0.3f, false), .transform = {.position = {-1.0f, 0.0f, -0.6f}}},
    });
    scene.camera().position = {0.0f, 1.6f, 4.0f};
    scene.camera().lookAt({0.0f, 0.0f, -0.4f});
    scene.light().direction = {-0.5f, -1.0f, -0.4f};
    sample.animate = [near, edge, back, left](Scene &s, float t) {
        s.at(near).transform().rotation = spin(0.7f * t + 0.9f, {1, 0.2f, 0});
        s.at(edge).transform().rotation = spin(-0.5f * t + 0.4f, {0, 0.3f, 1});
        s.at(back).transform().rotation = spin(0.4f * t + 1.2f, {1, 1, 0});
        s.at(left).transform().rotation = spin(0.9f * t, {0.2f, 0, 1});
    };
    return sample;
}

// A benchmark rather than a picture: a 24 x 24 grid of 65k-triangle tori (37.7 million triangles), seen
// from inside the grid so that many tori are cut by the frustum. Far too heavy for the CPU rasterizers.
SampleScene field() {
    SampleScene sample;
    Scene &scene = sample.scene;
    auto torus = std::make_shared<Mesh>(Mesh::torus(0.6f, 0.22f, 256, 128));
    constexpr int kGrid = 24;
    constexpr float kSpacing = 2.0f;
    for (int x = 0; x < kGrid; ++x) {
        for (int z = 0; z < kGrid; ++z) {
            const float hue = static_cast<float>((x * 7 + z * 3) % 10) / 10.0f;
            const vec4 albedo = srgb(0.4f + 0.5f * hue, 0.35f + 0.4f * (1.0f - hue), 0.5f + 0.3f * std::sin(6.0f * hue));
            const vec3 position{(static_cast<float>(x) - 0.5f * (kGrid - 1)) * kSpacing, 0.0f,
                                (static_cast<float>(z) - 0.5f * (kGrid - 1)) * kSpacing};
            scene.addObject({.mesh = torus,
                             .material = {.albedo = albedo, .roughness = 0.6f, .doubleSided = false},
                             .transform = {.position = position}});
        }
    }
    scene.camera().position = {0.0f, 2.5f, 6.0f};
    scene.camera().lookAt({0.0f, 0.0f, -4.0f});
    sample.animate = [](Scene &s, float t) {
        // Nothing is removed, so the objects stay in the order they were added.
        for (std::size_t i = 0; i < s.objectCount(); ++i) {
            s.objects()[i].transform().rotation = spin(0.5f * t + 0.37f * static_cast<float>(i), {1, 0.5f, 0.25f});
        }
    };
    return sample;
}

// A tangent-space normal map of round bumps in a @p cells x @p cells grid, from the gradient of a height field.
std::shared_ptr<Texture> bumpNormalMap(std::uint32_t size, std::uint32_t cells, float strength) {
    const auto height = [&](float x, float y) {
        const float u = std::fmod(x * static_cast<float>(cells) / static_cast<float>(size), 1.0f) - 0.5f;
        const float v = std::fmod(y * static_cast<float>(cells) / static_cast<float>(size), 1.0f) - 0.5f;
        const float r2 = (u * u + v * v) * 4.0f;
        return r2 < 1.0f ? std::sqrt(1.0f - r2) : 0.0f;
    };
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float fx = static_cast<float>(x) + 0.5f;
            const float fy = static_cast<float>(y) + 0.5f;
            const float dx = (height(fx + 1.0f, fy) - height(fx - 1.0f, fy)) * 0.5f;
            const float dy = (height(fx, fy + 1.0f) - height(fx, fy - 1.0f)) * 0.5f;
            // +y of the map points along +v (down the image), so the y slope keeps its sign here.
            const vec3 n = glm::normalize(vec3(-dx * strength, -dy * strength, 1.0f));
            std::uint8_t *texel = &rgba[(static_cast<std::size_t>(y) * size + x) * 4];
            for (int c = 0; c < 3; ++c) {
                texel[c] = static_cast<std::uint8_t>(std::lround((n[c] * 0.5f + 0.5f) * 255.0f));
            }
            texel[3] = 255;
        }
    }
    return std::make_shared<Texture>("bumps", size, size, std::move(rgba), TextureEncoding::Normal);
}

// Opaque white with round holes (alpha 0) in a @p cells x @p cells grid, for alpha masking.
std::shared_ptr<Texture> holesTexture(std::uint32_t size, std::uint32_t cells) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4, 255);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float u = std::fmod((static_cast<float>(x) + 0.5f) * cells / size, 1.0f) - 0.5f;
            const float v = std::fmod((static_cast<float>(y) + 0.5f) * cells / size, 1.0f) - 0.5f;
            rgba[(static_cast<std::size_t>(y) * size + x) * 4 + 3] = u * u + v * v < 0.09f ? 0 : 255;
        }
    }
    return std::make_shared<Texture>("holes", size, size, std::move(rgba), TextureEncoding::Srgb);
}

// A benchmark of object count rather than triangles: 32 x 32 x 32 small spinning cubes (32,768 objects,
// 393k triangles), so the per-object work of a renderer shows.
SampleScene crowd() {
    SampleScene sample;
    Scene &scene = sample.scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(0.3f));
    constexpr int kSide = 32;
    constexpr float kSpacing = 0.6f;
    const std::array<Material, 4> paints{paint(0.9f, 0.25f, 0.2f, false), paint(0.3f, 0.75f, 0.35f, false),
                                         paint(0.25f, 0.45f, 0.9f, false), paint(0.9f, 0.8f, 0.3f, false)};
    for (int x = 0; x < kSide; ++x) {
        for (int y = 0; y < kSide; ++y) {
            for (int z = 0; z < kSide; ++z) {
                const vec3 position = (vec3(x, y, z) - 0.5f * (kSide - 1)) * kSpacing;
                scene.addObject({.mesh = cube,
                                 .material = paints[static_cast<std::size_t>(x + y + z) % paints.size()],
                                 .transform = {.position = position}});
            }
        }
    }
    scene.camera().position = {6.0f, 5.0f, 16.0f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    sample.animate = [](Scene &s, float t) {
        // Nothing is removed, so the objects stay in the order they were added.
        for (std::size_t i = 0; i < s.objectCount(); ++i) {
            s.objects()[i].transform().rotation = spin(1.3f * t + 0.11f * static_cast<float>(i), {1, 0.7f, 0.3f});
        }
    };
    return sample;
}

// The material model: a row of gold spheres from smooth to rough, a row of red plastic ones, a textured
// and normal-mapped sphere, a screen with holes (alpha mask), a glass sphere (alpha blend) and a glowing
// cube, on a checkered floor.
SampleScene materials() {
    SampleScene sample;
    Scene &scene = sample.scene;
    auto sphere = std::make_shared<Mesh>(Mesh::sphere(0.35f, 32, 16));

    // Nearest magnification keeps the squares' edges sharp close up; mips still smooth them far away.
    auto checker = std::make_shared<Texture>(Texture::checker("floor checker", 64, 8, srgb(0.75f, 0.75f, 0.78f),
                                                              srgb(0.35f, 0.35f, 0.4f), TextureEncoding::Srgb,
                                                              SamplerDesc{.magFilter = TextureFilter::Nearest}));
    scene.addObject({.mesh = std::make_shared<Mesh>(Mesh::plane(8.0f)),
                     .material = {.albedo = vec4(1.0f), .roughness = 0.8f, .doubleSided = false, .baseColorTexture = checker},
                     .transform = {.position = {0.0f, -0.4f, -0.5f}}});

    constexpr int kSteps = 5;
    for (int i = 0; i < kSteps; ++i) {
        const float roughness = 0.1f + 0.85f * static_cast<float>(i) / (kSteps - 1);
        const float x = (static_cast<float>(i) - 0.5f * (kSteps - 1)) * 0.8f;
        scene.addObjects({
            // gold
            {.mesh = sphere,
             .material = {.albedo = srgb(1.0f, 0.78f, 0.34f), .metallic = 1.0f, .roughness = roughness, .doubleSided = false},
             .transform = {.position = {x, 0.0f, -1.3f}}},
            // red plastic
            {.mesh = sphere,
             .material = {.albedo = srgb(0.8f, 0.1f, 0.1f), .roughness = roughness, .doubleSided = false},
             .transform = {.position = {x, 0.0f, -2.2f}}},
        });
    }

    auto tiles = std::make_shared<Texture>(
        Texture::checker("tiles", 128, 4, srgb(0.9f, 0.85f, 0.75f), srgb(0.2f, 0.45f, 0.6f)));
    // Metallic-roughness: g roughness, b metallic. The light squares are polished metal, the dark ones rough.
    auto tileSurface = std::make_shared<Texture>(Texture::checker(
        "tile surface", 128, 4, vec4(0.0f, 0.25f, 1.0f, 1.0f), vec4(0.0f, 0.9f, 0.0f, 1.0f), TextureEncoding::Linear));
    const auto [textured, screen, glass, glow] = scene.addObjects({
        {.mesh = std::make_shared<Mesh>(Mesh::sphere(0.55f, 48, 24)),
         .material = {.albedo = vec4(1.0f), .metallic = 1.0f, .roughness = 1.0f, .doubleSided = false,
                      .baseColorTexture = tiles, .metallicRoughnessTexture = tileSurface,
                      .normalTexture = bumpNormalMap(128, 8, 6.0f)},
         .transform = {.position = {-1.0f, 0.15f, 0.2f}}},
        // An upright screen (a plane turned to face the camera), double-sided, its holes cut by the mask.
        {.mesh = std::make_shared<Mesh>(Mesh::plane(1.0f)),
         .material = {.albedo = srgb(0.3f, 0.6f, 0.95f), .roughness = 0.5f, .alphaMode = AlphaMode::Mask,
                      .baseColorTexture = holesTexture(64, 4)},
         .transform = {.position = {0.55f, 0.15f, -0.3f}, .rotation = spin(glm::half_pi<float>(), {1, 0, 0})}},
        {.mesh = sphere,
         .material = {.albedo = srgb(0.6f, 0.85f, 1.0f, 0.35f), .roughness = 0.05f, .alphaMode = AlphaMode::Blend,
                      .doubleSided = false},
         .transform = {.position = {1.1f, 0.05f, 0.6f}}},
        {.mesh = std::make_shared<Mesh>(Mesh::cube(0.3f)),
         .material = {.albedo = srgb(0.2f, 0.1f, 0.05f), .roughness = 0.9f, .emissive = vec3(4.0f, 1.6f, 0.4f),
                      .doubleSided = false},
         .transform = {.position = {0.1f, -0.2f, 0.9f}}},
    });
    static_cast<void>(screen); // these two stand still
    static_cast<void>(glass);

    scene.camera().position = {0.0f, 1.4f, 3.2f};
    scene.camera().lookAt({0.0f, -0.1f, -0.6f});
    scene.light().direction = {-0.6f, -1.0f, -0.5f};
    sample.animate = [textured, glow](Scene &s, float t) {
        s.at(textured).transform().rotation = spin(0.5f * t + 0.6f, {0, 1, 0});
        s.at(glow).transform().rotation = spin(0.8f * t + 0.3f, {0.2f, 1, 0.1f});
    };
    return sample;
}

} // namespace

std::string_view sampleSceneName(SampleSceneId id) {
    switch (id) {
    case SampleSceneId::Triangle: return "triangle";
    case SampleSceneId::Cubes: return "cubes";
    case SampleSceneId::Tori: return "tori";
    case SampleSceneId::Materials: return "materials";
    case SampleSceneId::Field: return "field";
    case SampleSceneId::Crowd: return "crowd";
    }
    throw std::invalid_argument("unknown SampleSceneId");
}

std::optional<SampleSceneId> parseSampleScene(std::string_view name) {
    const auto it = std::ranges::find(kSampleScenes, name, sampleSceneName);
    return it != kSampleScenes.end() ? std::optional(*it) : std::nullopt;
}

SampleScene makeSampleScene(SampleSceneId id) {
    SampleScene sample;
    switch (id) {
    case SampleSceneId::Triangle: sample = triangle(); break;
    case SampleSceneId::Cubes: sample = cubes(); break;
    case SampleSceneId::Tori: sample = tori(); break;
    case SampleSceneId::Materials: sample = materials(); break;
    case SampleSceneId::Field: sample = field(); break;
    case SampleSceneId::Crowd: sample = crowd(); break;
    }
    sample.name = sampleSceneName(id);
    sample.animate(sample.scene, 0.0f);
    return sample;
}

SampleScene makeMeshViewerScene(std::shared_ptr<const Mesh> mesh, std::string name) {
    SampleScene sample;
    sample.name = std::move(name);
    const Bounds bounds = mesh->bounds();
    const vec3 size = bounds.size();
    const float largest = std::max({size.x, size.y, size.z, 1e-6f});
    const float scale = 2.0f / largest;
    // Pivot about the mesh's centre: the turntable spins the centred mesh.
    const ObjectId id = sample.scene.addObject({.mesh = std::move(mesh), .material = paint(0.8f, 0.75f, 0.7f)});
    sample.scene.camera().position = {0.0f, 0.8f, 3.6f};
    sample.scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    sample.animate = [id, center = bounds.center(), scale](Scene &s, float t) {
        Transform &transform = s.at(id).transform();
        transform.rotation = spin(0.6f * t, {0, 1, 0});
        transform.scale = vec3(scale);
        transform.position = transform.rotation * (-center * scale);
    };
    sample.animate(sample.scene, 0.0f);
    return sample;
}

SampleScene makeGltfViewerScene(const std::filesystem::path &path) {
    SampleScene sample;
    sample.name = path.stem().string();
    const GltfImport import = loadGltf(path, sample.scene);
    if (import.objects.empty()) {
        throw std::runtime_error(path.string() + " has nothing to draw");
    }
    const vec3 size = import.bounds.size();
    const float scale = 2.0f / std::max({size.x, size.y, size.z, 1e-6f});
    // Each object keeps its place in the model; the turntable turns the whole model about its centre.
    std::vector<std::pair<ObjectId, mat4>> placed;
    for (ObjectId id : import.objects) placed.emplace_back(id, sample.scene.at(id).transform().matrix());
    sample.scene.camera().position = {0.0f, 0.8f, 3.6f};
    sample.scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    sample.animate = [placed, center = import.bounds.center(), scale](Scene &s, float t) {
        const mat4 turntable = glm::mat4_cast(spin(0.6f * t, {0, 1, 0})) * glm::scale(mat4(1.0f), vec3(scale)) *
                               glm::translate(mat4(1.0f), -center);
        for (const auto &[id, model] : placed) s.at(id).transform() = Transform::fromMatrix(turntable * model);
    };
    sample.animate(sample.scene, 0.0f);
    return sample;
}

} // namespace phox
