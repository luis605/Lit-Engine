module;

import Engine.glm;

export module Engine.camera;

export enum class CameraMovement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

export struct ViewOrientation {
    glm::mat3 frame{1.0f};
    bool framed = false;
    float yaw = -90.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    bool relaxing = false;
    float rollRate = 0.0f;
    float pitchRate = 0.0f;
};

export void orientationBasis(const ViewOrientation& orientation, glm::vec3& front, glm::vec3& up, glm::vec3& right);
export void decomposeOrientation(ViewOrientation& orientation, const glm::vec3& front, const glm::vec3& up, float yawHint);
export void rotateOrientation(ViewOrientation& orientation, const glm::mat3& rotation);
export void canonicalOrientation(ViewOrientation& orientation);
export bool relaxOrientation(ViewOrientation& orientation, const glm::vec3& targetUp, float deltaSeconds, float seconds, float pitchLimit = 89.0f);
export [[nodiscard]] glm::mat3 rotationBetween(const glm::vec3& from, const glm::vec3& to);

export class Camera {
   public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f, float nearPlane = 0.1f, float farPlane = 100.0f);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return m_position; }

    void processKeyboard(CameraMovement direction, float deltaTime);
    void processMouseMovement(float horizontalOffset, float verticalOffset, bool constrainPitch = true);
    void updateAspectRatio(float width, float height);
    void setNearPlane(float nearPlane) { m_nearPlane = nearPlane; }
    void setFarPlane(float farPlane) { m_farPlane = farPlane; }
    void setPos(glm::vec3 position) { m_position = position; }
    void setOrientation(float yaw, float pitch) { setOrientation(yaw, pitch, 0.0f); }
    void setOrientation(float yaw, float pitch, float roll);
    void setFov(float fov) { m_fov = fov; }
    float getFov() const { return m_fov; }
    float getYaw() const { return m_orientation.yaw; }
    float getPitch() const { return m_orientation.pitch; }
    float getRoll() const { return m_orientation.roll; }
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }
    void setOrthographic(bool orthographic) { m_orthographic = orthographic; }
    bool isOrthographic() const { return m_orthographic; }
    void setOrthoHeight(float height) { m_orthoHeight = height; }
    float getOrthoHeight() const { return m_orthoHeight; }

    glm::vec3 getFront() const { return m_front; }
    glm::vec3 getUp() const { return m_up; }
    glm::vec3 getRight() const { return m_right; }
    glm::quat getRotation() const;
    void setRotation(const glm::quat& rotation);
    void setRotation(const glm::mat3& rotation);
    void setFrame(const glm::mat3& frame);
    const glm::mat3& getFrame() const { return m_orientation.frame; }
    bool hasFrame() const { return m_orientation.framed; }
    glm::vec3 getFrameUp() const { return m_worldUp; }
    const ViewOrientation& orientation() const { return m_orientation; }
    void setOrientationState(const ViewOrientation& orientation);
    void transformBy(const glm::mat4& transform);
    void rotateBy(const glm::mat3& rotation);
    bool relaxFrame(const glm::vec3& targetUp, float deltaSeconds, float seconds);
    bool relaxing() const { return m_orientation.relaxing; }

   private:
    void updateCameraVectors();

    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    ViewOrientation m_orientation;

    float m_movementSpeed;
    float m_mouseSensitivity;

    float m_fov;
    float m_aspectRatio;
    float m_nearPlane;
    float m_farPlane;
    bool m_orthographic = false;
    float m_orthoHeight = 10.0f;
};
