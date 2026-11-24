module;

import Engine.glm;

export module Engine.camera;

export enum class CameraMovement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

// Full view orientation (docs/Portals.md, "Camera crossing"): a reference frame (rotation whose +Y is the "up" the controls
// yaw around; identity = world Y up) and yaw / pitch / roll in degrees relative to it. Yaw -90 / pitch 0 / roll 0 looks down -Z.
// A portal crossing rotates the frame and leaves the angles alone, so any rigid transform is applied exactly; relaxOrientation
// turns a tilted frame back toward gravity-up without moving the view direction (the difference becomes roll, which decays).
export struct ViewOrientation {
    glm::mat3 frame{1.0f};
    bool framed = false;  // frame != identity
    float yaw = -90.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    bool relaxing = false;  // a tilted frame was turned upright and the leftover roll / pitch is still decaying
    float rollRate = 0.0f;   // degrees per second of the relax (critically damped: starts and ends smoothly)
    float pitchRate = 0.0f;
};

// Front, up and right of an orientation (world space).
export void orientationBasis(const ViewOrientation& orientation, glm::vec3& front, glm::vec3& up, glm::vec3& right);
// Re-express a world front / up in `frame`: yaw / pitch / roll (degrees, yaw kept within 180 of `yawHint`).
export void decomposeOrientation(ViewOrientation& orientation, const glm::vec3& front, const glm::vec3& up, float yawHint);
// Rotate the orientation by a rigid rotation (portal transform): frame = rotation * frame, angles unchanged.
export void rotateOrientation(ViewOrientation& orientation, const glm::mat3& rotation);
// Fold a frame that is (numerically) upright back into identity and the angles, so upright crossings keep the classic yaw/pitch state.
export void canonicalOrientation(ViewOrientation& orientation);
// Turn the frame up toward `targetUp` keeping the view, then bring the leftover roll (and pitch beyond +-pitchLimit) to 0 with a
// critically damped motion that settles in about `seconds` (<= 0: at once). Returns true while still relaxing.
export bool relaxOrientation(ViewOrientation& orientation, const glm::vec3& targetUp, float deltaSeconds, float seconds, float pitchLimit = 89.0f);
// Rotation from a to b (unit vectors); a rotation of 180 degrees about an axis perpendicular to a when they are opposite.
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
    // Angles relative to the reference frame (identity unless a tilted portal crossing or setFrame changed it). Roll is 0 here.
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

    // World basis of the view.
    glm::vec3 getFront() const { return m_front; }
    glm::vec3 getUp() const { return m_up; }
    glm::vec3 getRight() const { return m_right; }
    // Camera-to-world rotation (columns right, up, -front).
    glm::quat getRotation() const;
    // Set the world orientation; it is decomposed into yaw / pitch / roll relative to the current frame.
    void setRotation(const glm::quat& rotation);
    void setRotation(const glm::mat3& rotation);
    // Reference frame: its +Y is the up that mouse yaw and UP / DOWN movement use.
    void setFrame(const glm::mat3& frame);
    const glm::mat3& getFrame() const { return m_orientation.frame; }
    bool hasFrame() const { return m_orientation.framed; }
    glm::vec3 getFrameUp() const { return m_worldUp; }
    const ViewOrientation& orientation() const { return m_orientation; }
    void setOrientationState(const ViewOrientation& orientation);
    // Rigid transform (portal crossing): position and orientation exactly, the view matrix becomes view * inverse(transform).
    void transformBy(const glm::mat4& transform);
    // Orientation only (rotation part of a rigid transform).
    void rotateBy(const glm::mat3& rotation);
    // See relaxOrientation. Returns true while relaxing.
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
