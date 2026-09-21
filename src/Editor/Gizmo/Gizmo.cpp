module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <optional>

module Editor.gizmo;

import Engine.input;

namespace {
const glm::vec3 kAxes[3] = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
const glm::vec4 kColors[3] = {glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), glm::vec4(0.3f, 0.5f, 1.0f, 1.0f)};

float distanceToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 1.0e-6f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const glm::vec2 closest = a + ab * t;
    return glm::length(p - closest);
}

float closestParameterOnAxis(const Ray& ray, const glm::vec3& origin, const glm::vec3& axis) {
    const glm::vec3 w0 = ray.origin - origin;
    const float a = glm::dot(ray.direction, ray.direction);
    const float b = glm::dot(ray.direction, axis);
    const float c = glm::dot(axis, axis);
    const float d = glm::dot(ray.direction, w0);
    const float e = glm::dot(axis, w0);
    const float denom = a * c - b * b;
    if (std::abs(denom) < 1.0e-6f) return 0.0f;
    return (a * e - b * d) / denom;
}
}

bool Gizmo::update(Engine& engine, History& history, EntityHandle selected, bool enabled) {
    World& world = engine.world();
    if (!world.isAlive(selected) || !enabled) {
        if (m_activeAxis >= 0 && world.isAlive(m_target)) history.commitLocalMatrix(m_target, m_startLocal, world.getLocalMatrix(m_target));
        m_activeAxis = -1;
        m_hoverAxis = -1;
        return false;
    }

    const glm::vec3 origin = world.getWorldPosition(selected);
    const float length = std::max(glm::length(origin - world.camera().getPosition()) * 0.15f, 0.05f);
    const glm::vec2 mouse = InputManager::GetMousePosition();
    const Ray ray = engine.screenRay(mouse.x, mouse.y);

    if (m_activeAxis < 0) {
        m_hoverAxis = -1;
        float best = 12.0f;
        const auto start = engine.worldToScreen(origin);
        for (int a = 0; a < 3 && start; ++a) {
            const auto end = engine.worldToScreen(origin + kAxes[a] * length);
            if (!end) continue;
            const float d = distanceToSegment(mouse, *start, *end);
            if (d < best) {
                best = d;
                m_hoverAxis = a;
            }
        }
        if (m_hoverAxis >= 0 && InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            m_activeAxis = m_hoverAxis;
            m_target = selected;
            m_startLocal = world.getLocalMatrix(selected);
            m_startWorldPosition = origin;
            m_startParameter = closestParameterOnAxis(ray, origin, kAxes[m_activeAxis]);
        }
    }

    bool consumed = m_activeAxis >= 0 || m_hoverAxis >= 0 && InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    if (m_activeAxis >= 0) {
        if (InputManager::IsMouseButtonHeld(GLFW_MOUSE_BUTTON_LEFT) || InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            const float parameter = closestParameterOnAxis(ray, m_startWorldPosition, kAxes[m_activeAxis]);
            glm::mat4 worldMatrix = world.getWorldMatrix(m_target);
            worldMatrix[3] = glm::vec4(m_startWorldPosition + kAxes[m_activeAxis] * (parameter - m_startParameter), 1.0f);
            world.setWorldMatrix(m_target, worldMatrix);
        } else {
            history.commitLocalMatrix(m_target, m_startLocal, world.getLocalMatrix(m_target));
            m_activeAxis = -1;
        }
    }

    for (int a = 0; a < 3; ++a) {
        const bool highlighted = a == m_hoverAxis || a == m_activeAxis;
        engine.debugLine(origin, origin + kAxes[a] * length, highlighted ? glm::vec4(1.0f, 0.9f, 0.1f, 1.0f) : kColors[a]);
    }
    return consumed;
}
