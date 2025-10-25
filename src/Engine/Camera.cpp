import glm;
import camera;

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position), m_front(glm::vec3(0.0f, 0.0f, -1.0f)), m_worldUp(up), m_yaw(yaw),
      m_pitch(pitch), m_movementSpeed(2.5f), m_mouseSensitivity(0.1f), m_fov(45.0f),
      m_aspectRatio(1280.0f / 720.0f), m_nearPlane(0.1f), m_farPlane(100.0f) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    return glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    float velocity = m_movementSpeed * deltaTime;
    if (direction == CameraMovement::FORWARD)
        m_position += m_front * velocity;
    if (direction == CameraMovement::BACKWARD)
        m_position -= m_front * velocity;
    if (direction == CameraMovement::LEFT)
        m_position -= m_right * velocity;
    if (direction == CameraMovement::RIGHT)
        m_position += m_right * velocity;
    if (direction == CameraMovement::UP)
        m_position += m_worldUp * velocity;
    if (direction == CameraMovement::DOWN)
        m_position -= m_worldUp * velocity;
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= m_mouseSensitivity;
    yoffset *= m_mouseSensitivity;

    m_yaw += xoffset;
    m_pitch += yoffset;

    if (constrainPitch) {
        if (m_pitch > 89.0f)
            m_pitch = 89.0f;
        if (m_pitch < -89.0f)
            m_pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::updateAspectRatio(float width, float height) { m_aspectRatio = width / height; }

void Camera::updateCameraVectors() {
    glm::vec3 front;
    front.x = glm::cos(glm::radians(m_yaw)) * glm::cos(glm::radians(m_pitch));
    front.y = glm::sin(glm::radians(m_pitch));
    front.z = glm::sin(glm::radians(m_yaw)) * glm::cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);

    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}