module;

#include <algorithm>
#include <cmath>
#include <optional>

module Engine.CameraRig;

import Engine.camera;

namespace {
constexpr int kMaxSelfSkips = 8;
constexpr float kSkipBias = 1.0e-3f;
}

void CameraRig::setMode(CameraMode mode) {
    if (mode == m_mode) return;
    const bool detached = !m_world.isAlive(m_target);
    if (detached && m_mode == CameraMode::FirstPerson) m_pivot = m_world.camera().getPosition();
    m_mode = mode;
    if (detached && mode == CameraMode::FirstPerson) m_world.camera().setPos(m_pivot);
    update();
}

CameraMode CameraRig::toggle() {
    setMode(m_mode == CameraMode::FirstPerson ? CameraMode::ThirdPerson : CameraMode::FirstPerson);
    return m_mode;
}

glm::vec3 CameraRig::pivot() const {
    if (m_world.isAlive(m_target)) return m_world.getWorldPosition(m_target) + glm::vec3(0.0f, m_settings.eyeHeight, 0.0f);
    return m_mode == CameraMode::FirstPerson ? m_world.camera().getPosition() : m_pivot;
}

glm::vec3 CameraRig::forward() const {
    const Camera& camera = m_world.camera();
    const float yaw = glm::radians(camera.getYaw());
    const float pitch = glm::radians(camera.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

void CameraRig::look(float yawDeltaDegrees, float pitchDeltaDegrees) {
    Camera& camera = m_world.camera();
    const float pitch = std::clamp(camera.getPitch() + pitchDeltaDegrees, -m_settings.maxPitch, m_settings.maxPitch);
    camera.setOrientation(camera.getYaw() + yawDeltaDegrees, pitch);
}

float CameraRig::collide(const glm::vec3& pivotPoint, const glm::vec3& direction, float wanted) {
    const float reach = wanted + m_settings.collisionRadius;
    if (m_query) {
        const auto hit = m_query(pivotPoint, direction, reach);
        return hit ? std::clamp(*hit - m_settings.collisionRadius, 0.0f, wanted) : wanted;
    }
    float travelled = 0.0f;
    for (int attempt = 0; attempt < kMaxSelfSkips; ++attempt) {
        const auto hit = m_world.raycast(pivotPoint + direction * travelled, direction, reach - travelled);
        if (!hit) return wanted;
        const float hitDistance = travelled + hit->distance;
        if (m_world.isAlive(m_target) && (hit->entity == m_target || m_world.isDescendantOf(hit->entity, m_target))) {
            travelled = hitDistance + kSkipBias;
            continue;
        }
        return std::clamp(hitDistance - m_settings.collisionRadius, 0.0f, wanted);
    }
    return wanted;
}

void CameraRig::update() {
    Camera& camera = m_world.camera();
    const glm::vec3 pivotPoint = pivot();
    if (!m_world.isAlive(m_target)) m_pivot = pivotPoint;
    if (m_mode == CameraMode::FirstPerson) {
        m_distance = 0.0f;
        camera.setPos(pivotPoint);
        return;
    }
    const glm::vec3 forwardDirection = forward();
    const glm::vec3 right = glm::normalize(glm::cross(forwardDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 anchor = pivotPoint + right * m_settings.shoulderOffset;
    const glm::vec3 back = -forwardDirection;
    float distance = collide(anchor, back, m_settings.thirdDistance);
    if (distance < m_settings.minDistance) distance = std::min(m_settings.minDistance, m_settings.thirdDistance);
    m_distance = distance;
    camera.setPos(anchor + back * distance);
}
