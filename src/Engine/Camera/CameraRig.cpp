module;

#include <algorithm>
#include <cmath>
#include <optional>

module Engine.CameraRig;

import Engine.camera;

void CameraRig::setMode(CameraMode mode) {
    if (mode == m_mode) return;
    const bool free = !m_world.isAlive(m_target);
    if (free && m_mode == CameraMode::FirstPerson) m_pivot = m_world.camera().getPosition();
    m_mode = mode;
    if (free && mode == CameraMode::FirstPerson) m_world.camera().setPos(m_pivot);
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
    const Camera& c = m_world.camera();
    const float yaw = glm::radians(c.getYaw());
    const float pitch = glm::radians(c.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

void CameraRig::look(float yawDeltaDegrees, float pitchDeltaDegrees) {
    Camera& c = m_world.camera();
    const float pitch = std::clamp(c.getPitch() + pitchDeltaDegrees, -m_settings.maxPitch, m_settings.maxPitch);
    c.setOrientation(c.getYaw() + yawDeltaDegrees, pitch);
}

float CameraRig::collide(const glm::vec3& pivotPoint, const glm::vec3& direction, float wanted) {
    const float reach = wanted + m_settings.collisionRadius;
    if (m_query) {
        const auto hit = m_query(pivotPoint, direction, reach);
        return hit ? std::clamp(*hit - m_settings.collisionRadius, 0.0f, wanted) : wanted;
    }
    float travelled = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const auto hit = m_world.raycast(pivotPoint + direction * travelled, direction, reach - travelled);
        if (!hit) return wanted;
        const float at = travelled + hit->distance;
        if (m_world.isAlive(m_target) && (hit->entity == m_target || m_world.isDescendantOf(hit->entity, m_target))) {
            travelled = at + 1.0e-3f;
            continue;
        }
        return std::clamp(at - m_settings.collisionRadius, 0.0f, wanted);
    }
    return wanted;
}

void CameraRig::update() {
    Camera& c = m_world.camera();
    const glm::vec3 p = pivot();
    if (!m_world.isAlive(m_target)) m_pivot = p;
    if (m_mode == CameraMode::FirstPerson) {
        m_distance = 0.0f;
        c.setPos(p);
        return;
    }
    const glm::vec3 fwd = forward();
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 anchor = p + right * m_settings.shoulderOffset;
    glm::vec3 back = -fwd;
    float distance = collide(anchor, back, m_settings.thirdDistance);
    if (distance < m_settings.minDistance) distance = std::min(m_settings.minDistance, m_settings.thirdDistance);
    m_distance = distance;
    c.setPos(anchor + back * distance);
}
