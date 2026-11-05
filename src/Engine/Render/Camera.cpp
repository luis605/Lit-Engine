import Engine.glm;
import Engine.camera;

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch, float nearPlane, float farPlane)
    : m_position(position), m_front(glm::vec3(0.0f, 0.0f, -1.0f)), m_worldUp(up), m_yaw(yaw),
      m_pitch(pitch), m_movementSpeed(2.5f), m_mouseSensitivity(0.1f), m_fov(45.0f),
      m_aspectRatio(1280.0f / 720.0f), m_nearPlane(nearPlane), m_farPlane(farPlane) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    if (m_orthographic) {
        const float halfHeight = m_orthoHeight * 0.5f;
        const float halfWidth = halfHeight * m_aspectRatio;
        return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, m_nearPlane, m_farPlane);
    }
    return glm::perspectiveRH_ZO(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    const float distance = m_movementSpeed * deltaTime;
    switch (direction) {
        case CameraMovement::FORWARD: m_position += m_front * distance; break;
        case CameraMovement::BACKWARD: m_position -= m_front * distance; break;
        case CameraMovement::LEFT: m_position -= m_right * distance; break;
        case CameraMovement::RIGHT: m_position += m_right * distance; break;
        case CameraMovement::UP: m_position += m_worldUp * distance; break;
        case CameraMovement::DOWN: m_position -= m_worldUp * distance; break;
    }
}

void Camera::processMouseMovement(float horizontalOffset, float verticalOffset, bool constrainPitch) {
    constexpr float kMaxPitchDegrees = 89.0f;

    m_yaw += horizontalOffset * m_mouseSensitivity;
    m_pitch += verticalOffset * m_mouseSensitivity;

    if (constrainPitch) m_pitch = glm::clamp(m_pitch, -kMaxPitchDegrees, kMaxPitchDegrees);

    updateCameraVectors();
}

void Camera::updateAspectRatio(float width, float height) {
    if (height <= 0.0f) return;
    m_aspectRatio = width / height;
}

void Camera::updateCameraVectors() {
    const float yawRadians = glm::radians(m_yaw);
    const float pitchRadians = glm::radians(m_pitch);
    const float pitchCosine = glm::cos(pitchRadians);
    m_front = glm::normalize(glm::vec3(glm::cos(yawRadians) * pitchCosine, glm::sin(pitchRadians), glm::sin(yawRadians) * pitchCosine));

    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}