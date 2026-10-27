#include "phox/Camera.hpp"

namespace phox {

void Camera::lookAt(const vec3 &target, const vec3 &up) {
    orientation = glm::quatLookAtRH(glm::normalize(target - position), up);
}

vec3 Camera::forward() const { return orientation * vec3(0.0f, 0.0f, -1.0f); }

mat4 Camera::view() const {
    // The inverse of the camera's world transform (rotation, then translation).
    return glm::mat4_cast(glm::conjugate(orientation)) * glm::translate(mat4(1.0f), -position);
}

mat4 Camera::projection(float aspect) const { return perspective(fovY, aspect, nearZ, farZ); }

} // namespace phox
