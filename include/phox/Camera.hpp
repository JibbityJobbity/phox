#ifndef PHOX_CAMERA_HPP
#define PHOX_CAMERA_HPP

#include <array>

namespace phox {

class Camera {
public:
    Camera() = default;

    void setPosition(std::array<float, 3> position) { m_position = position; }
    void setFovY(float fovY) { m_fovY = fovY; }
    void setAspectRatio(float aspectRatio) { m_aspectRatio = aspectRatio; }

    const std::array<float, 3>& position() const { return m_position; }
    float fovY() const { return m_fovY; }
    float aspectRatio() const { return m_aspectRatio; }

private:
    std::array<float, 3> m_position{0.0f, 0.0f, 1.0f};
    float m_fovY = 60.0f;
    float m_aspectRatio = 16.0f / 9.0f;
};

} // namespace phox

#endif // PHOX_CAMERA_HPP
