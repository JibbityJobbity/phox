#include "Check.hpp"

#include "phox/scene/Camera.hpp"

#include <array>
#include <cmath>

using phox::scene::Camera;

namespace {

struct Clip {
    float x, y, z, w;
};

// Column-major 4x4 times (x, y, z, 1).
Clip transform(const std::array<float, 16> &m, float x, float y, float z) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14], m[3] * x + m[7] * y + m[11] * z + m[15]};
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

int main() {
    // The baseline camera is a fixed matrix: scene (0, 0)..(1, 1) lands on the top-right quadrant.
    const std::array<float, 16> baseline{2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(Camera::unitOrtho().viewProjection() == baseline);

    // A perspective camera at (0, 0, 5) looking down -z.
    Camera camera;
    camera.setPosition({0.0f, 0.0f, 5.0f});
    camera.setFovY(60.0f);
    camera.setAspectRatio(2.0f);
    camera.setClipPlanes(0.1f, 100.0f);
    const std::array<float, 16> m = camera.viewProjection();

    // The point straight ahead is at the centre of the picture, inside the [0, 1] depth range.
    const Clip centre = transform(m, 0.0f, 0.0f, 0.0f);
    CHECK(near(centre.x / centre.w, 0.0f) && near(centre.y / centre.w, 0.0f));
    CHECK(centre.z / centre.w > 0.0f && centre.z / centre.w < 1.0f);
    CHECK(near(centre.w, 5.0f)); // w is the distance along the view direction

    // Vulkan clip space: +y in the world is up in the picture, which is negative y in clip space.
    const Clip above = transform(m, 0.0f, 1.0f, 0.0f);
    CHECK(above.y / above.w < 0.0f);
    // +x is to the right.
    CHECK(transform(m, 1.0f, 0.0f, 0.0f).x > 0.0f);

    // Half the vertical field of view (30 degrees) at distance 5 reaches the top edge.
    const float edge = 5.0f * std::tan(30.0f * 3.14159265358979f / 180.0f);
    const Clip top = transform(m, 0.0f, edge, 0.0f);
    CHECK(near(top.y / top.w, -1.0f));
    // The aspect ratio widens the horizontal range accordingly: with aspect 2 the same offset spans half.
    const Clip side = transform(m, edge, 0.0f, 0.0f);
    CHECK(near(side.x / side.w, 0.5f));

    // Near and far planes map to depth 0 and 1.
    const Clip nearPoint = transform(m, 0.0f, 0.0f, 5.0f - 0.1f);
    const Clip farPoint = transform(m, 0.0f, 0.0f, 5.0f - 100.0f);
    CHECK(near(nearPoint.z / nearPoint.w, 0.0f));
    CHECK(near(farPoint.z / farPoint.w, 1.0f));

    std::cout << "camera test passed" << std::endl;
    return 0;
}
