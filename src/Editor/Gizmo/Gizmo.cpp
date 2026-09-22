module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

module Editor.gizmo;

import Engine.input;
import Engine.GizmoMath;

namespace {
const glm::vec4 kColors[3] = {glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), glm::vec4(0.3f, 0.5f, 1.0f, 1.0f)};
const glm::vec4 kHighlight(1.0f, 0.9f, 0.1f, 1.0f);
constexpr int kRingSegments = 48;

void ringBasis(const glm::vec3& axis, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(axis, helper));
    v = glm::cross(axis, u);
}
}

std::string Gizmo::label() const {
    const char* mode = m_mode == GizmoMode::Translate ? "Move" : (m_mode == GizmoMode::Rotate ? "Rotate" : "Scale");
    return std::string(mode) + (m_mode == GizmoMode::Scale ? " (local)" : (m_local ? " (local)" : " (world)"));
}

void Gizmo::commitDrag(World& world, History& history) {
    history.beginGroup();
    for (const Start& start : m_starts) {
        if (world.isAlive(start.entity)) history.commitLocalMatrix(start.entity, start.local, world.getLocalMatrix(start.entity));
    }
    history.endGroup();
    m_starts.clear();
}

bool Gizmo::update(Engine& engine, History& history, EntityHandle selected, const std::vector<EntityHandle>& targets, bool enabled) {
    World& world = engine.world();
    if (!world.isAlive(selected) || !enabled) {
        if (m_activeAxis >= 0) commitDrag(world, history);
        m_activeAxis = -1;
        m_hoverAxis = -1;
        return false;
    }

    const bool ctrl = InputManager::IsKeyHeld(GLFW_KEY_LEFT_CONTROL) || InputManager::IsKeyHeld(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT) || InputManager::IsKeyHeld(GLFW_KEY_RIGHT_SHIFT);

    if (m_activeAxis < 0) {
        if (InputManager::IsKeyPressed(GLFW_KEY_TAB)) m_mode = static_cast<GizmoMode>((static_cast<int>(m_mode) + 1) % 3);
        if (InputManager::IsKeyPressed(GLFW_KEY_X)) m_local = !m_local;
    }

    const glm::vec3 origin = world.getWorldPosition(selected);
    const bool useLocal = m_local || m_mode == GizmoMode::Scale;
    const glm::vec3 axes[3] = {useLocal ? world.right(selected) : glm::vec3(1.0f, 0.0f, 0.0f), useLocal ? world.up(selected) : glm::vec3(0.0f, 1.0f, 0.0f), useLocal ? -world.forward(selected) : glm::vec3(0.0f, 0.0f, 1.0f)};
    const float length = std::max(glm::length(origin - world.camera().getPosition()) * 0.15f, 0.05f);
    const glm::vec2 mouse = InputManager::GetMousePosition();
    const Ray ray = engine.screenRay(mouse.x, mouse.y);
    const bool pressed = InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);

    if (m_activeAxis < 0) {
        m_hoverAxis = -1;
        float best = 12.0f;
        for (int a = 0; a < 3; ++a) {
            float distance = 1.0e9f;
            if (m_mode == GizmoMode::Rotate) {
                glm::vec3 u, v;
                ringBasis(axes[a], u, v);
                std::optional<glm::vec2> previous;
                for (int i = 0; i <= kRingSegments; ++i) {
                    const float angle = 6.28318530718f * static_cast<float>(i) / kRingSegments;
                    const auto point = engine.worldToScreen(origin + (u * std::cos(angle) + v * std::sin(angle)) * length);
                    if (previous && point) distance = std::min(distance, gizmo::distanceToSegment2D(mouse, *previous, *point));
                    previous = point;
                }
            } else {
                const auto start = engine.worldToScreen(origin);
                const auto end = engine.worldToScreen(origin + axes[a] * length);
                if (start && end) distance = gizmo::distanceToSegment2D(mouse, *start, *end);
            }
            if (distance < best) {
                best = distance;
                m_hoverAxis = a;
            }
        }
        if (m_hoverAxis >= 0 && pressed) {
            bool started = true;
            m_starts.clear();
            for (EntityHandle t : targets) {
                if (world.isAlive(t)) m_starts.push_back({t, world.getLocalMatrix(t), world.getWorldMatrix(t)});
            }
            if (m_starts.empty()) started = false;
            m_pivot = origin;
            m_axisDirection = axes[m_hoverAxis];
            m_length = length;
            if (m_mode == GizmoMode::Rotate) {
                if (const auto p = gizmo::rayPlane(ray, origin, m_axisDirection)) {
                    m_startVector = *p - origin;
                } else {
                    started = false;
                }
            } else {
                m_startParameter = gizmo::closestParameterOnAxis(ray, origin, m_axisDirection);
            }
            if (started) m_activeAxis = m_hoverAxis;
        }
    }

    const bool consumed = m_activeAxis >= 0 || (m_hoverAxis >= 0 && pressed);
    if (m_activeAxis >= 0) {
        if (InputManager::IsMouseButtonHeld(GLFW_MOUSE_BUTTON_LEFT) || pressed) {
            if (m_mode == GizmoMode::Translate) {
                float delta = gizmo::closestParameterOnAxis(ray, m_pivot, m_axisDirection) - m_startParameter;
                if (ctrl) delta = gizmo::snap(delta, 0.5f);
                for (const Start& start : m_starts) {
                    glm::mat4 matrix = start.world;
                    matrix[3] = glm::vec4(glm::vec3(start.world[3]) + m_axisDirection * delta, 1.0f);
                    world.setWorldMatrix(start.entity, matrix);
                }
            } else if (m_mode == GizmoMode::Rotate) {
                if (const auto p = gizmo::rayPlane(ray, m_pivot, m_axisDirection)) {
                    float angle = gizmo::signedAngleAroundAxis(m_startVector, *p - m_pivot, m_axisDirection);
                    if (ctrl) angle = gizmo::snap(angle, glm::radians(15.0f));
                    for (const Start& start : m_starts) world.setWorldMatrix(start.entity, gizmo::rotateAboutWorldAxis(start.world, m_pivot, m_axisDirection, angle));
                }
            } else {
                float factor = std::max(0.01f, 1.0f + (gizmo::closestParameterOnAxis(ray, m_pivot, m_axisDirection) - m_startParameter) / m_length);
                if (ctrl) factor = std::max(0.1f, gizmo::snap(factor, 0.1f));
                for (const Start& start : m_starts) world.setLocalMatrix(start.entity, gizmo::scaleAlongLocalAxis(start.local, m_activeAxis, factor, shift));
            }
        } else {
            commitDrag(world, history);
            m_activeAxis = -1;
        }
    }

    const glm::vec3 drawOrigin = m_activeAxis >= 0 ? m_pivot : origin;
    for (int a = 0; a < 3; ++a) {
        const bool highlighted = a == m_hoverAxis || a == m_activeAxis;
        const glm::vec4 color = highlighted ? kHighlight : kColors[a];
        if (m_mode == GizmoMode::Rotate) {
            glm::vec3 u, v;
            ringBasis(axes[a], u, v);
            for (int i = 0; i < kRingSegments; ++i) {
                const float a0 = 6.28318530718f * static_cast<float>(i) / kRingSegments;
                const float a1 = 6.28318530718f * static_cast<float>(i + 1) / kRingSegments;
                engine.debugLine(drawOrigin + (u * std::cos(a0) + v * std::sin(a0)) * length, drawOrigin + (u * std::cos(a1) + v * std::sin(a1)) * length, color, true);
            }
        } else {
            const glm::vec3 end = drawOrigin + axes[a] * length;
            engine.debugLine(drawOrigin, end, color, true);
            if (m_mode == GizmoMode::Scale) engine.debugBox(end, glm::vec3(length * 0.06f), color, true);
        }
    }
    return consumed;
}
