#ifndef PHOX_MATH_HPP
#define PHOX_MATH_HPP

// Phox's vector and matrix types. Everything includes glm through this header so the configuration is
// the same everywhere (the build also defines these for every target that links Phox).
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan clip space: depth in [0, 1]
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace phox {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;

/// Right-handed perspective projection into Vulkan clip space: depth 0 at @p nearZ and 1 at @p farZ,
/// and +y in view space pointing up the screen (Vulkan's clip-space y points down, so it is flipped).
/// @param fovY  vertical field of view in radians
inline mat4 perspective(float fovY, float aspect, float nearZ, float farZ) {
    mat4 projection = glm::perspectiveRH_ZO(fovY, aspect, nearZ, farZ);
    projection[1][1] *= -1.0f;
    return projection;
}

} // namespace phox

#endif // PHOX_MATH_HPP
