module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module camera;

export enum class CameraMovement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

export class Camera {
  public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return m_position; }

    void processKeyboard(CameraMovement direction, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void updateAspectRatio(float width, float height);

  private:
    void updateCameraVectors();

    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    float m_yaw;
    float m_pitch;

    float m_movementSpeed;
    float m_mouseSensitivity;

    float m_fov;
    float m_aspectRatio;
    float m_nearPlane;
    float m_farPlane;
};