module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <vector>

module Editor.gizmo;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr float NUDGE_SHIFT_MULTIPLIER = 10.0f;
constexpr float MIN_VECTOR_LENGTH_SQUARED = 1e-12f;

glm::mat4 translation(const glm::vec3& offset) { return glm::translate(glm::mat4(1.0f), offset); }

glm::vec3 dominantHorizontalAxis(glm::vec3 vector) {
    vector.y = 0.0f;
    if (std::abs(vector.x) >= std::abs(vector.z)) return glm::vec3(vector.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
    return glm::vec3(0.0f, 0.0f, vector.z >= 0.0f ? 1.0f : -1.0f);
}

ImGuizmo::OPERATION operationFor(HandleMode mode) {
    return mode == HandleMode::Translate ? ImGuizmo::TRANSLATE : mode == HandleMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
}

const char* handleLabel(HandleMode mode) { return mode == HandleMode::Translate ? "Move" : mode == HandleMode::Rotate ? "Rotate" : "Scale"; }

glm::mat4 projectionFor(const Camera& camera, const glm::vec2& size) {
    const float aspect = size.x / std::max(size.y, 1.0f);
    if (!camera.isOrthographic()) return glm::perspective(glm::radians(camera.getFov()), aspect, camera.getNearPlane(), camera.getFarPlane());
    const float halfHeight = camera.getOrthoHeight() * 0.5f;
    return glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, camera.getNearPlane(), camera.getFarPlane());
}

}

void TransformTool::drawHandles(EditorContext& ctx) {
    const EntityHandle primary = ctx.primary();
    if (ctx.handles == HandleMode::Off || m_kind != ModalKind::None || !ctx.world.isAlive(primary) || ctx.flying) {
        m_handleDragging = false;
        return;
    }
    const glm::vec2 size = ctx.engine.windowSize();
    const Camera& camera = ctx.world.camera();
    ImGuizmo::SetOrthographic(camera.isOrthographic());
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, size.x, size.y);
    ImGuizmo::AllowAxisFlip(false);
    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 projection = projectionFor(camera, size);
    glm::mat4 matrix = ctx.world.getWorldMatrix(primary);
    const ImGuizmo::MODE mode = ctx.localSpace || ctx.handles == HandleMode::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    const bool snap = ctx.snapping != ImGui::GetIO().KeyCtrl;
    const float step = ctx.handles == HandleMode::Translate ? ctx.snapMove : ctx.handles == HandleMode::Rotate ? ctx.snapAngle : ctx.snapScale;
    const float snapValues[3] = {step, step, step};
    const bool changed = ImGuizmo::Manipulate(&view[0][0], &projection[0][0], operationFor(ctx.handles), mode, &matrix[0][0], nullptr, snap ? snapValues : nullptr);
    const bool dragging = ImGuizmo::IsUsing();
    if (dragging && !m_handleDragging) {
        m_handleDragging = true;
        capture(ctx);
        m_handleStarts = m_starts;
        m_handlePrimaryStart = ctx.world.getWorldMatrix(primary);
    }
    if (m_handleDragging && changed) {
        const glm::mat4 delta = matrix * glm::inverse(m_handlePrimaryStart);
        for (const Start& start : m_handleStarts) {
            const glm::mat4 world = start.entity == primary ? matrix : delta * start.world;
            ctx.world.setLocalMatrix(start.entity, glm::inverse(start.parentWorld) * world);
        }
    }
    if (!dragging && m_handleDragging) {
        m_handleDragging = false;
        m_starts = m_handleStarts;
        commit(ctx, handleLabel(ctx.handles));
    }
}

void TransformTool::nudge(EditorContext& ctx, const glm::vec3& screenDirection) {
    const std::vector<EntityHandle> top = ctx.topLevelSelection();
    if (top.empty()) return;
    const glm::vec3 forward = cameraForward(ctx.world.camera());
    const glm::vec3 rightRaw = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 right = glm::dot(rightRaw, rightRaw) > MIN_VECTOR_LENGTH_SQUARED ? glm::normalize(rightRaw) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 direction = dominantHorizontalAxis(right) * screenDirection.x + dominantHorizontalAxis(forward) * screenDirection.z + glm::vec3(0.0f, screenDirection.y, 0.0f);
    const float step = ctx.snapMove * (ImGui::GetIO().KeyShift ? NUDGE_SHIFT_MULTIPLIER : 1.0f);
    const glm::mat4 shift = translation(direction * step);
    ctx.history->beginGroup("Nudge");
    for (EntityHandle entity : top) {
        const EntityHandle parent = ctx.world.getParent(entity);
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : ctx.world.getWorldMatrix(parent);
        ctx.history->setLocalMatrix(entity, glm::inverse(parentWorld) * shift * ctx.world.getWorldMatrix(entity));
    }
    ctx.history->endGroup();
}

void TransformTool::resetTransform(EditorContext& ctx, int part) {
    const std::vector<EntityHandle> top = ctx.topLevelSelection();
    if (top.empty()) return;
    ctx.history->beginGroup(part == 0 ? "Reset position" : part == 1 ? "Reset rotation" : "Reset scale");
    for (EntityHandle entity : top) {
        const glm::vec3 position = part == 0 ? glm::vec3(0.0f) : ctx.world.getPosition(entity);
        const glm::quat rotation = part == 1 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : ctx.world.getRotation(entity);
        const glm::vec3 scale = part == 2 ? glm::vec3(1.0f) : ctx.world.getScale(entity);
        ctx.history->setLocalMatrix(entity, translation(position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale));
    }
    ctx.history->endGroup();
}
