#include "phox/SampleScenes.hpp"

#include <algorithm>
#include <stdexcept>

namespace phox {

namespace {

quat spin(float radians, const vec3 &axis) { return glm::angleAxis(radians, glm::normalize(axis)); }

SampleScene triangle() {
    SampleScene sample;
    sample.name = "triangle";
    Scene &scene = sample.scene;
    scene.addObject("triangle", std::make_shared<Mesh>(Mesh::triangle(1.0f)), Material{"pink", {1.0f, 0.2f, 0.4f, 1.0f}});
    scene.camera().position = {0.0f, 0.0f, 3.0f};
    scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    scene.light().direction = {-0.4f, -0.6f, -1.0f};
    sample.animate = [](Scene &s, float t) { s.find("triangle")->transform().rotation = spin(1.2f * t, {0, 1, 0}); };
    return sample;
}

SampleScene cubes() {
    SampleScene sample;
    sample.name = "cubes";
    Scene &scene = sample.scene;
    auto cube = std::make_shared<Mesh>(Mesh::cube(1.0f));
    scene.addObject("floor", std::make_shared<Mesh>(Mesh::plane(8.0f)), Material{"floor", {0.55f, 0.55f, 0.6f, 1.0f}},
                    Transform{{0.0f, -0.5f, 0.0f}});
    scene.addObject("red", cube, Material{"red", {0.9f, 0.25f, 0.2f, 1.0f}}, Transform{{0.0f, 0.0f, 0.0f}});
    scene.addObject("green", cube, Material{"green", {0.3f, 0.75f, 0.35f, 1.0f}}, Transform{{-1.6f, 0.0f, -0.8f}});
    scene.addObject("blue", cube, Material{"blue", {0.25f, 0.45f, 0.9f, 1.0f}}, Transform{{1.6f, 0.0f, -0.8f}});
    scene.addObject("yellow", cube, Material{"yellow", {0.9f, 0.8f, 0.3f, 1.0f}},
                    Transform{{0.0f, 0.25f, -2.2f}, quat(1.0f, 0.0f, 0.0f, 0.0f), {1.0f, 1.5f, 1.0f}});
    // In front of the red cube and overlapping it on screen, so depth testing has something to do.
    scene.addObject("purple", cube, Material{"purple", {0.6f, 0.35f, 0.85f, 1.0f}},
                    Transform{{0.55f, -0.25f, 1.0f}, quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(0.5f)});
    scene.camera().position = {0.0f, 2.2f, 5.0f};
    scene.camera().lookAt({0.0f, 0.0f, -0.3f});
    scene.light().direction = {-0.5f, -1.0f, -0.35f};
    sample.animate = [](Scene &s, float t) {
        s.find("red")->transform().rotation = spin(0.8f * t + 0.4f, {0, 1, 0});
        s.find("green")->transform().rotation = spin(-0.6f * t, {0, 1, 0});
        s.find("blue")->transform().rotation = spin(0.5f * t + 0.3f, {0.3f, 1, 0});
        s.find("purple")->transform().rotation = spin(1.5f * t + 0.6f, {1, 1, 0});
    };
    return sample;
}

} // namespace

std::vector<std::string> sampleSceneNames() { return {"triangle", "cubes"}; }

SampleScene makeSampleScene(const std::string &name) {
    SampleScene sample;
    if (name == "triangle") {
        sample = triangle();
    } else if (name == "cubes") {
        sample = cubes();
    } else {
        throw std::invalid_argument("unknown sample scene '" + name + "' (try: triangle, cubes)");
    }
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
    sample.scene.addObject("mesh", std::move(mesh), Material{"clay", {0.8f, 0.75f, 0.7f, 1.0f}});
    sample.scene.camera().position = {0.0f, 0.8f, 3.6f};
    sample.scene.camera().lookAt({0.0f, 0.0f, 0.0f});
    sample.animate = [center = bounds.center(), scale](Scene &s, float t) {
        Transform &transform = s.find("mesh")->transform();
        transform.rotation = spin(0.6f * t, {0, 1, 0});
        transform.scale = vec3(scale);
        transform.position = transform.rotation * (-center * scale);
    };
    sample.animate(sample.scene, 0.0f);
    return sample;
}

} // namespace phox
