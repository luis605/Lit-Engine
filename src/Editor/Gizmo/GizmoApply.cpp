module;

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>

module Editor.gizmo;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.GizmoMath;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr float MIN_ROTATE_RADIUS_PIXELS = 2.0f;
constexpr float MIN_SCALE_START_DISTANCE = 4.0f;
constexpr float MIN_COLUMN_LENGTH = 1e-6f;

glm::mat4 translation(const glm::vec3& offset) { return glm::translate(glm::mat4(1.0f), offset); }

std::optional<glm::vec3> planeHit(const Ray& ray, const glm::vec3& point, const glm::vec3& normal) { return gizmo::rayPlane(ray, point, normal); }

float snapValue(float value, float step) { return step > 0.0f ? std::round(value / step) * step : value; }

}

void TransformTool::apply(EditorContext& ctx) {
    const bool snap = ctx.snapping != ImGui::GetIO().KeyCtrl;
    const std::optional<double> typed = m_typed.empty() ? std::nullopt : evaluateExpression(m_typed);
    const glm::vec3 axis = axisDirection(ctx);
    if (m_kind == ModalKind::Move) {
        applyMove(ctx, axis, typed, snap);
    } else if (m_kind == ModalKind::Rotate) {
        applyRotate(ctx, axis, typed, snap);
    } else {
        applyScale(ctx, axis, typed, snap);
    }
}

void TransformTool::applyMove(EditorContext& ctx, const glm::vec3& axis, const std::optional<double>& typed, bool snap) {
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    const Ray startRay = ctx.engine.screenRay(m_startMouse.x, m_startMouse.y);
    glm::vec3 delta(0.0f);
    if (typed) {
        const glm::vec3 direction = m_axis < 0 ? glm::vec3(1.0f, 0.0f, 0.0f) : axis;
        delta = direction * static_cast<float>(*typed);
    } else if (m_axis >= 0 && !m_plane) {
        const float current = gizmo::closestParameterOnAxis(ray, m_pivot, axis);
        const float initial = gizmo::closestParameterOnAxis(startRay, m_pivot, axis);
        float distance = current - initial;
        if (snap) distance = snapValue(distance, ctx.snapMove);
        delta = axis * distance;
    } else {
        const glm::vec3 normal = m_axis >= 0 ? axis : cameraForward(ctx.world.camera());
        const std::optional<glm::vec3> startPoint = planeHit(startRay, m_pivot, normal);
        const std::optional<glm::vec3> currentPoint = planeHit(ray, m_pivot, normal);
        if (startPoint && currentPoint) delta = *currentPoint - *startPoint;
        if (snap) delta = glm::vec3(snapValue(delta.x, ctx.snapMove), snapValue(delta.y, ctx.snapMove), snapValue(delta.z, ctx.snapMove));
    }
    m_delta = delta;
    const glm::mat4 shift = translation(delta);
    for (const Start& start : m_starts) ctx.world.setLocalMatrix(start.entity, glm::inverse(start.parentWorld) * shift * start.world);
}

void TransformTool::applyRotate(EditorContext& ctx, const glm::vec3& axis, const std::optional<double>& typed, bool snap) {
    const glm::vec2 current = ctx.mouse - m_pivotScreen;
    if (glm::length(current) > MIN_ROTATE_RADIUS_PIXELS) {
        const glm::vec2 direction = glm::normalize(current);
        const float step = std::atan2(m_lastAngleVector.x * direction.y - m_lastAngleVector.y * direction.x, glm::dot(m_lastAngleVector, direction));
        if (std::isfinite(step)) m_accumulatedAngle += step;
        m_lastAngleVector = direction;
    }
    const float facing = glm::dot(axis, cameraForward(ctx.world.camera())) >= 0.0f ? 1.0f : -1.0f;
    float degrees = typed ? static_cast<float>(*typed) : glm::degrees(m_accumulatedAngle) * facing;
    if (snap && !typed) degrees = snapValue(degrees, ctx.snapAngle);
    m_value = degrees;
    const glm::quat rotationQuat = glm::angleAxis(glm::radians(typed ? degrees : -degrees), axis);
    const glm::mat4 rotation = translation(m_pivot) * glm::mat4_cast(rotationQuat) * translation(-m_pivot);
    for (const Start& start : m_starts) ctx.world.setLocalMatrix(start.entity, glm::inverse(start.parentWorld) * rotation * start.world);
}

void TransformTool::applyScale(EditorContext& ctx, const glm::vec3& axis, const std::optional<double>& typed, bool snap) {
    const float startDistance = std::max(glm::length(m_startMouse - m_pivotScreen), MIN_SCALE_START_DISTANCE);
    float factor = typed ? static_cast<float>(*typed) : glm::length(ctx.mouse - m_pivotScreen) / startDistance;
    if (snap && !typed) factor = std::max(snapValue(factor, ctx.snapScale), ctx.snapScale);
    m_value = factor;
    for (const Start& start : m_starts) {
        glm::vec3 offset = glm::vec3(start.world[3]) - m_pivot;
        glm::mat4 world = start.world;
        if (m_axis < 0) {
            offset *= factor;
            world[0] *= factor;
            world[1] *= factor;
            world[2] *= factor;
        } else {
            offset += axis * (glm::dot(offset, axis) * (factor - 1.0f));
            for (int column = 0; column < 3; ++column) {
                const glm::vec3 basis(world[column]);
                const float along = glm::length(basis) > MIN_COLUMN_LENGTH ? std::abs(glm::dot(glm::normalize(basis), axis)) : 0.0f;
                world[column] = glm::vec4(basis * (1.0f + (factor - 1.0f) * along), 0.0f);
            }
        }
        world[3] = glm::vec4(m_pivot + offset, 1.0f);
        ctx.world.setLocalMatrix(start.entity, glm::inverse(start.parentWorld) * world);
    }
}
