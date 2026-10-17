#ifndef PHOX_SCENE_CAMERA_HPP
#define PHOX_SCENE_CAMERA_HPP

#include <array>

namespace phox::scene {

class Camera {
  public:
    Camera() = default;

    /// A fixed camera that maps the scene square [0, 1] x [0, 1] onto the top-right quadrant of a target
    /// with +y pointing up the picture. It is the camera the baseline (golden) image was rendered with.
    static Camera unitOrtho();

    void setPosition(std::array<float, 3> position) { m_position = position; }
    void setFovY(float fovYDegrees) { m_fovY = fovYDegrees; }
    void setAspectRatio(float aspectRatio) { m_aspectRatio = aspectRatio; }
    void setClipPlanes(float nearPlane, float farPlane) {
        m_near = nearPlane;
        m_far = farPlane;
    }

    const std::array<float, 3> &position() const { return m_position; }
    float fovY() const { return m_fovY; }
    float aspectRatio() const { return m_aspectRatio; }
    float nearPlane() const { return m_near; }
    float farPlane() const { return m_far; }

    /// Column-major view-projection matrix (the layout a GLSL `mat4` uniform expects), for a camera at
    /// `position` looking down -z with +y up. Clip space follows Vulkan: y points down, depth is [0, 1].
    std::array<float, 16> viewProjection() const;

  private:
    std::array<float, 3> m_position{0.0f, 0.0f, 1.0f};
    float m_fovY = 60.0f; // degrees
    float m_aspectRatio = 16.0f / 9.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
    bool m_unitOrtho = false;
};

} // namespace phox::scene

#endif // PHOX_SCENE_CAMERA_HPP
