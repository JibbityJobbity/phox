#ifndef PHOX_CAMERA_HPP
#define PHOX_CAMERA_HPP

#include "phox/Math.hpp"

namespace phox {

/// A perspective camera. With the identity orientation it looks down -Z with +Y up.
class Camera {
public:
    vec3 position{0.0f, 0.0f, 2.0f};
    quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    float fovY = glm::radians(60.0f); ///< vertical field of view, radians
    float nearZ = 0.1f;
    float farZ = 100.0f;

    /// Turns the camera to face @p target. @p up must not be parallel to the view direction.
    void lookAt(const vec3 &target, const vec3 &up = {0.0f, 1.0f, 0.0f});
    vec3 forward() const;
    mat4 view() const;
    /// Projection into Vulkan clip space for a target with the given width / height.
    mat4 projection(float aspect) const;
};

} // namespace phox

#endif // PHOX_CAMERA_HPP
