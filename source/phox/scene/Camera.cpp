#include "phox/scene/Camera.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace phox::scene {

Camera Camera::unitOrtho() {
    Camera camera;
    camera.m_unitOrtho = true;
    return camera;
}

std::array<float, 16> Camera::viewProjection() const {
    if (m_unitOrtho) {
        return {2.0f, 0.0f, 0.0f, 0.0f, //
                0.0f, -2.0f, 0.0f, 0.0f, //
                0.0f, 0.0f, 1.0f, 0.0f, //
                0.0f, 0.0f, 0.0f, 1.0f};
    }
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), -glm::vec3(m_position[0], m_position[1], m_position[2]));
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(m_fovY), m_aspectRatio, m_near, m_far);
    projection[1][1] *= -1.0f; // Vulkan clip space has +y pointing down
    const glm::mat4 viewProjection = projection * view;

    std::array<float, 16> result{};
    std::copy_n(glm::value_ptr(viewProjection), 16, result.begin());
    return result;
}

} // namespace phox::scene
