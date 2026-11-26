module;

#include <algorithm>
#include <cmath>
#include <limits>

module Engine.camera;

import Engine.glm;

namespace {
constexpr float kDegenerateCosine = 1.0e-6f;
constexpr float kDecomposeCosine = 1.0e-5f;
constexpr float kRollSnapDegrees = 1.0e-3f;
constexpr float kCanonicalRollSnapDegrees = 1.0e-2f;
constexpr float kUprightSine = 1.0e-4f;
constexpr float kAlignedSine = 5.0e-4f;
constexpr float kRelaxRate = 9.0f;
constexpr float kRelaxDoneRate = 1.0f;
constexpr float kRelaxDoneDegrees = 0.02f;
constexpr float kMaxPitchDegrees = 89.0f;
const glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

bool isIdentity(const glm::mat3& matrix) {
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            if (std::abs(matrix[column][row] - (column == row ? 1.0f : 0.0f)) > 1.0e-7f) return false;
        }
    }
    return true;
}

glm::mat3 orthonormalize(const glm::mat3& matrix) {
    const glm::vec3 x = glm::normalize(matrix[0]);
    const glm::vec3 y = glm::normalize(matrix[1] - x * glm::dot(x, matrix[1]));
    return glm::mat3(x, y, glm::cross(x, y));
}

float wrapDegrees(float degrees) {
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return wrapped - 180.0f;
}
}

glm::mat3 rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float cosine = glm::dot(from, to);
    if (cosine < -0.999999f) {
        glm::vec3 axis = glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(axis, axis) < 1.0e-6f) axis = glm::cross(from, glm::vec3(0.0f, 0.0f, 1.0f));
        axis = glm::normalize(axis);
        glm::mat3 result(1.0f);
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) result[column][row] = 2.0f * axis[column] * axis[row] - (column == row ? 1.0f : 0.0f);
        }
        return result;
    }
    const glm::vec3 v = glm::cross(from, to);
    if (glm::dot(v, v) < 1.0e-24f) return glm::mat3(1.0f);
    const glm::mat3 skew(glm::vec3(0.0f, v.z, -v.y), glm::vec3(-v.z, 0.0f, v.x), glm::vec3(v.y, -v.x, 0.0f));
    return orthonormalize(glm::mat3(1.0f) + skew + skew * skew * (1.0f / (1.0f + cosine)));
}

void orientationBasis(const ViewOrientation& orientation, glm::vec3& front, glm::vec3& up, glm::vec3& right) {
    const float yawRadians = glm::radians(orientation.yaw);
    const float pitchRadians = glm::radians(orientation.pitch);
    const float pitchCosine = glm::cos(pitchRadians);
    front = glm::normalize(glm::vec3(glm::cos(yawRadians) * pitchCosine, glm::sin(pitchRadians), glm::sin(yawRadians) * pitchCosine));
    const bool degenerate = std::abs(pitchCosine) <= kDegenerateCosine;
    right = degenerate ? glm::vec3(-glm::sin(yawRadians), 0.0f, glm::cos(yawRadians)) : glm::normalize(glm::cross(front, kWorldUp));
    up = glm::normalize(glm::cross(right, front));
    if (orientation.roll != 0.0f) {
        const float rollRadians = glm::radians(orientation.roll);
        const float rollCosine = glm::cos(rollRadians);
        const float rollSine = glm::sin(rollRadians);
        const glm::vec3 baseRight = right;
        right = baseRight * rollCosine + up * rollSine;
        up = up * rollCosine - baseRight * rollSine;
    }
    if (orientation.framed) {
        front = orientation.frame * front;
        up = orientation.frame * up;
        right = orientation.frame * right;
    }
}

void decomposeOrientation(ViewOrientation& orientation, const glm::vec3& front, const glm::vec3& up, float yawHint) {
    const glm::mat3 toLocal = glm::transpose(orientation.frame);
    const glm::vec3 localFront = orientation.framed ? toLocal * front : front;
    const glm::vec3 localUp = orientation.framed ? toLocal * up : up;
    const float horizontal = std::sqrt(localFront.x * localFront.x + localFront.z * localFront.z);
    if (horizontal > kDecomposeCosine) {
        orientation.pitch = glm::degrees(std::asin(glm::clamp(localFront.y, -1.0f, 1.0f)));
        orientation.yaw = glm::degrees(std::atan2(localFront.z, localFront.x));
        const float yawRadians = glm::radians(orientation.yaw);
        const glm::vec3 baseRight(-glm::sin(yawRadians), 0.0f, glm::cos(yawRadians));
        const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, glm::normalize(localFront)));
        orientation.roll = glm::degrees(std::atan2(-glm::dot(localUp, baseRight), glm::dot(localUp, baseUp)));
    } else {
        const float sign = localFront.y >= 0.0f ? 1.0f : -1.0f;
        orientation.pitch = 90.0f * sign;
        orientation.yaw = glm::degrees(std::atan2(-sign * localUp.z, -sign * localUp.x));
        orientation.roll = 0.0f;
    }
    if (std::isfinite(yawHint)) orientation.yaw += 360.0f * std::round((yawHint - orientation.yaw) / 360.0f);
}

void canonicalOrientation(ViewOrientation& orientation) {
    if (!orientation.framed) return;
    if (isIdentity(orientation.frame)) {
        orientation.frame = glm::mat3(1.0f);
        orientation.framed = false;
        return;
    }
    const glm::vec3 frameUp = orientation.frame[1];
    if (frameUp.y <= 0.0f || glm::length(glm::cross(frameUp, kWorldUp)) >= kUprightSine) return;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    orientationBasis(orientation, front, up, right);
    const float frameYaw = glm::degrees(std::atan2(-orientation.frame[0].z, orientation.frame[0].x));
    const float hint = orientation.yaw - frameYaw;
    const bool unrolled = orientation.roll == 0.0f;
    orientation.frame = glm::mat3(1.0f);
    orientation.framed = false;
    decomposeOrientation(orientation, front, up, hint);
    if (unrolled && std::abs(orientation.roll) < kCanonicalRollSnapDegrees) orientation.roll = 0.0f;
}

void rotateOrientation(ViewOrientation& orientation, const glm::mat3& rotation) {
    orientation.frame = orthonormalize(orientation.framed ? rotation * orientation.frame : rotation);
    orientation.framed = true;
    canonicalOrientation(orientation);
}

bool relaxOrientation(ViewOrientation& orientation, const glm::vec3& targetUp, float deltaSeconds, float seconds, float pitchLimit) {
    if (glm::dot(targetUp, targetUp) < 1.0e-12f) return orientation.relaxing;
    const glm::vec3 target = glm::normalize(targetUp);
    const glm::vec3 frameUp = orientation.framed ? orientation.frame[1] : kWorldUp;
    if (glm::dot(frameUp, target) < 0.0f || glm::length(glm::cross(frameUp, target)) > kAlignedSine) {
        glm::vec3 front;
        glm::vec3 up;
        glm::vec3 right;
        orientationBasis(orientation, front, up, right);
        const float hint = orientation.yaw;
        orientation.frame = orthonormalize(rotationBetween(frameUp, target) * (orientation.framed ? orientation.frame : glm::mat3(1.0f)));
        orientation.framed = true;
        decomposeOrientation(orientation, front, up, hint);
        orientation.relaxing = true;
        canonicalOrientation(orientation);
    }
    if (!orientation.relaxing) return false;
    const float limitPitch = glm::clamp(orientation.pitch, -pitchLimit, pitchLimit);
    float excess = orientation.pitch - limitPitch;
    orientation.roll = wrapDegrees(orientation.roll);
    if (seconds <= 0.0f) {
        orientation.roll = 0.0f;
        excess = 0.0f;
        orientation.rollRate = orientation.pitchRate = 0.0f;
    } else {
        const float k = kRelaxRate / seconds;
        const float dt = std::max(deltaSeconds, 0.0f);
        const float decay = std::exp(-k * dt);
        auto damp = [&](float& value, float& rate) {
            const float push = (rate + k * value) * dt;
            const float next = (value + push) * decay;
            rate = (rate - k * push) * decay;
            value = next;
        };
        damp(orientation.roll, orientation.rollRate);
        damp(excess, orientation.pitchRate);
    }
    orientation.pitch = limitPitch + excess;
    if (std::abs(orientation.roll) < kRelaxDoneDegrees && std::abs(orientation.rollRate) < kRelaxDoneRate && std::abs(excess) < kRelaxDoneDegrees) {
        orientation.roll = 0.0f;
        orientation.pitch = limitPitch;
        orientation.rollRate = orientation.pitchRate = 0.0f;
        orientation.relaxing = false;
    }
    return orientation.relaxing;
}

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch, float nearPlane, float farPlane)
    : m_position(position), m_front(glm::vec3(0.0f, 0.0f, -1.0f)), m_worldUp(up), m_movementSpeed(2.5f), m_mouseSensitivity(0.1f), m_fov(45.0f),
      m_aspectRatio(1280.0f / 720.0f), m_nearPlane(nearPlane), m_farPlane(farPlane) {
    m_orientation.yaw = yaw;
    m_orientation.pitch = pitch;
    if (glm::dot(up, up) > 1.0e-12f) {
        const glm::mat3 frame = rotationBetween(kWorldUp, glm::normalize(up));
        if (!isIdentity(frame)) {
            m_orientation.frame = frame;
            m_orientation.framed = true;
        }
    }
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    if (m_orthographic) {
        const float halfHeight = m_orthoHeight * 0.5f;
        const float halfWidth = halfHeight * m_aspectRatio;
        return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, m_farPlane, m_nearPlane);
    }
    return glm::perspectiveRH_ZO(glm::radians(m_fov), m_aspectRatio, m_farPlane, m_nearPlane);
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
    const float previous = m_orientation.pitch;
    m_orientation.yaw += horizontalOffset * m_mouseSensitivity;
    m_orientation.pitch += verticalOffset * m_mouseSensitivity;
    if (constrainPitch) m_orientation.pitch = glm::clamp(m_orientation.pitch, std::min(-kMaxPitchDegrees, previous), std::max(kMaxPitchDegrees, previous));
    updateCameraVectors();
}

void Camera::updateAspectRatio(float width, float height) {
    if (height <= 0.0f) return;
    m_aspectRatio = width / height;
}

void Camera::setOrientation(float yaw, float pitch, float roll) {
    m_orientation.yaw = yaw;
    m_orientation.pitch = pitch;
    m_orientation.roll = roll;
    m_orientation.relaxing = false;
    m_orientation.rollRate = m_orientation.pitchRate = 0.0f;
    updateCameraVectors();
}

glm::quat Camera::getRotation() const { return glm::quat_cast(glm::mat3(m_right, m_up, -m_front)); }

void Camera::setRotation(const glm::quat& rotation) { setRotation(glm::mat3_cast(rotation)); }

void Camera::setRotation(const glm::mat3& rotation) {
    const glm::vec3 front = -glm::normalize(rotation[2]);
    const glm::vec3 up = glm::normalize(rotation[1]);
    decomposeOrientation(m_orientation, front, up, std::numeric_limits<float>::quiet_NaN());
    if (std::abs(m_orientation.roll) < kRollSnapDegrees) m_orientation.roll = 0.0f;
    m_orientation.relaxing = false;
    updateCameraVectors();
}

void Camera::setFrame(const glm::mat3& frame) {
    m_orientation.framed = !isIdentity(frame);
    m_orientation.frame = m_orientation.framed ? orthonormalize(frame) : glm::mat3(1.0f);
    updateCameraVectors();
}

void Camera::setOrientationState(const ViewOrientation& orientation) {
    m_orientation = orientation;
    updateCameraVectors();
}

void Camera::transformBy(const glm::mat4& transform) {
    m_position = glm::vec3(transform * glm::vec4(m_position, 1.0f));
    rotateBy(glm::mat3(transform));
}

void Camera::rotateBy(const glm::mat3& rotation) {
    rotateOrientation(m_orientation, rotation);
    updateCameraVectors();
}

bool Camera::relaxFrame(const glm::vec3& targetUp, float deltaSeconds, float seconds) {
    const bool relaxing = relaxOrientation(m_orientation, targetUp, deltaSeconds, seconds, kMaxPitchDegrees);
    updateCameraVectors();
    return relaxing;
}

void Camera::updateCameraVectors() {
    orientationBasis(m_orientation, m_front, m_up, m_right);
    m_worldUp = m_orientation.framed ? m_orientation.frame[1] : kWorldUp;
}
