module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

module Editor.viewport;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.camera;
import Engine.input;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr float CLICK_SLOP = 5.0f;
constexpr double DOUBLE_CLICK_SECONDS = 0.35;
constexpr float HOVER_IDLE_INTERVAL = 0.25f;
constexpr float HOVER_MIN_INTERVAL = 0.04f;
constexpr float HOVER_MOVE_THRESHOLD = 0.5f;
constexpr float GROUND_RAY_EPSILON = 1e-4f;
constexpr float GROUND_MAX_DISTANCE = 5000.0f;
constexpr float DEFAULT_CURSOR_DISTANCE = 10.0f;
constexpr float DEFAULT_PIVOT_DISTANCE = 10.0f;
constexpr float MIN_ORBIT_DISTANCE = 0.5f;
constexpr float ORBIT_SENSITIVITY = 2.5f;
constexpr float MIN_FLY_SPEED = 0.1f;
constexpr float MAX_FLY_SPEED = 5000.0f;
constexpr float FLY_SPEED_WHEEL_BASE = 1.2f;
constexpr float FLY_BOOST_MULTIPLIER = 4.0f;
constexpr float FLY_SLOW_MULTIPLIER = 0.25f;
constexpr float ORTHO_ZOOM_STEP = 0.87f;
constexpr float ORTHO_ZOOM_STEP_FAST = 0.7f;
constexpr float MIN_ORTHO_HEIGHT = 0.01f;
constexpr float MAX_ORTHO_HEIGHT = 100000.0f;
constexpr float DOLLY_FRACTION = 0.15f;
constexpr float MIN_DOLLY_STEP = 0.05f;
constexpr float MAX_DOLLY_STEP = 5000.0f;
constexpr float DOLLY_FAST_MULTIPLIER = 3.0f;
constexpr size_t BOX_SELECTION_LIMIT = 100000;

glm::vec3 cameraRight(const Camera& camera) {
    return glm::normalize(glm::cross(cameraForward(camera), glm::vec3(0.0f, 1.0f, 0.0f)));
}

void applySelection(EditorContext& ctx, EntityHandle entity, const ImGuiIO& io) {
    if (io.KeyCtrl) {
        ctx.selection.toggle(entity);
    } else if (io.KeyShift) {
        ctx.selection.remove(entity);
        ctx.selection.add(entity);
    } else {
        ctx.selection.set(entity);
    }
}

void zoomWithWheel(EditorContext& ctx, const ImGuiIO& io) {
    Camera& camera = ctx.world.camera();
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    if (camera.isOrthographic()) {
        const float factor = std::pow(io.KeyShift ? ORTHO_ZOOM_STEP_FAST : ORTHO_ZOOM_STEP, io.MouseWheel);
        const glm::vec3 forward = cameraForward(camera);
        glm::vec3 offset = ray.origin - camera.getPosition();
        offset -= forward * glm::dot(offset, forward);
        camera.setOrthoHeight(std::clamp(camera.getOrthoHeight() * factor, MIN_ORTHO_HEIGHT, MAX_ORTHO_HEIGHT));
        camera.setPos(camera.getPosition() + offset * (1.0f - factor));
        return;
    }
    const float distance = ctx.cursorOnSurface ? glm::length(ctx.cursor - camera.getPosition()) : ctx.flySpeed * 2.0f;
    const float step = std::clamp(distance * DOLLY_FRACTION, MIN_DOLLY_STEP, MAX_DOLLY_STEP) * io.MouseWheel * (io.KeyShift ? DOLLY_FAST_MULTIPLIER : 1.0f);
    camera.setPos(camera.getPosition() + ray.direction * step);
}

}

glm::vec3 Viewport::pivotFor(EditorContext& ctx) const {
    const std::vector<EntityHandle> top = ctx.topLevelSelection();
    if (!top.empty()) {
        glm::vec3 sum(0.0f);
        for (EntityHandle entity : top) sum += ctx.world.getWorldPosition(entity);
        return sum / static_cast<float>(top.size());
    }
    if (ctx.cursorOnSurface) return ctx.cursor;
    const Camera& camera = ctx.world.camera();
    return camera.getPosition() + cameraForward(camera) * DEFAULT_PIVOT_DISTANCE;
}

void Viewport::cancelInteractions(EditorContext& ctx) {
    m_pressing = false;
    m_boxing = false;
    m_orbiting = false;
    m_panning = false;
    if (ctx.flying) {
        ctx.flying = false;
        ctx.engine.setMouseCaptured(false);
    }
}

void Viewport::update(EditorContext& ctx, float dt, bool mouseFree) {
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMousePosValid(&io.MousePos) && std::isfinite(io.MousePos.x) && std::isfinite(io.MousePos.y)) ctx.mouse = glm::vec2(io.MousePos.x, io.MousePos.y);
    ctx.viewportHovered = mouseFree;
    updateFlight(ctx, dt);
    updateFly(ctx, dt);
    if (!ctx.flying) updateOrbitPan(ctx);
    if (!ctx.flying && !m_orbiting && !m_panning) {
        updateHover(ctx, dt);
        if (!ctx.modalActive) handleClicks(ctx, mouseFree);
    }

    if (mouseFree && !ctx.flying && !m_orbiting && io.MouseWheel != 0.0f && !ctx.modalActive) {
        zoomWithWheel(ctx, io);
        ctx.flight.active = false;
    }
}

void Viewport::updateFly(EditorContext& ctx, float dt) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ctx.flying) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ctx.viewportHovered && !ctx.modalActive) {
            ctx.flying = true;
            ctx.flight.active = false;
            ctx.world.camera().setOrthographic(false);
            ctx.engine.setMouseCaptured(true);
        }
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ctx.flying = false;
        ctx.engine.setMouseCaptured(false);
        return;
    }
    Camera& camera = ctx.world.camera();
    const glm::vec2 delta = InputManager::GetMouseDelta();
    camera.processMouseMovement(delta.x, -delta.y);
    if (io.MouseWheel != 0.0f) ctx.flySpeed = std::clamp(ctx.flySpeed * std::pow(FLY_SPEED_WHEEL_BASE, io.MouseWheel), MIN_FLY_SPEED, MAX_FLY_SPEED);
    const float distance = ctx.flySpeed * (io.KeyShift ? FLY_BOOST_MULTIPLIER : 1.0f) * (io.KeyCtrl ? FLY_SLOW_MULTIPLIER : 1.0f) * dt;
    const glm::vec3 forward = cameraForward(camera);
    const glm::vec3 right = cameraRight(camera);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 move(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
    if (ImGui::IsKeyDown(ImGuiKey_E)) move += worldUp;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= worldUp;
    if (glm::dot(move, move) > 0.0f) camera.setPos(camera.getPosition() + glm::normalize(move) * distance);
    ctx.modeLabel = "FLY";
    ctx.hint("WASD", "move");
    ctx.hint("Q/E", "down/up");
    ctx.hint("Wheel", std::format("speed {:.1f}", ctx.flySpeed));
    ctx.hint("Shift/Ctrl", "fast/slow");
}

void Viewport::updateOrbitPan(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    Camera& camera = ctx.world.camera();
    if (!m_orbiting && !m_panning && ctx.viewportHovered && !ctx.modalActive) {
        const bool orbitStart = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt) || (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !io.KeyShift && io.KeyAlt);
        const bool panStart = ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !io.KeyAlt;
        if (orbitStart || panStart) {
            m_orbitPivot = pivotFor(ctx);
            m_orbitDistance = std::max(glm::length(camera.getPosition() - m_orbitPivot), MIN_ORBIT_DISTANCE);
            m_orbiting = orbitStart;
            m_panning = panStart;
            ctx.flight.active = false;
            if (orbitStart && ctx.autoOrtho && camera.isOrthographic()) {
                camera.setOrthographic(false);
                ctx.autoOrtho = false;
            }
        }
    }
    if (m_orbiting) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_orbiting = false;
            return;
        }
        camera.processMouseMovement(io.MouseDelta.x * ORBIT_SENSITIVITY, -io.MouseDelta.y * ORBIT_SENSITIVITY);
        camera.setPos(m_orbitPivot - cameraForward(camera) * m_orbitDistance);
        ctx.modeLabel = "ORBIT";
        ctx.hint("Drag", "orbit around selection");
    }
    if (m_panning) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_panning = false;
            return;
        }
        const glm::vec3 right = cameraRight(camera);
        const glm::vec3 up = glm::normalize(glm::cross(right, cameraForward(camera)));
        const glm::vec2 size = ctx.engine.windowSize();
        const float visibleHeight = camera.isOrthographic() ? camera.getOrthoHeight() : 2.0f * m_orbitDistance * std::tan(glm::radians(camera.getFov()) * 0.5f);
        const float unitsPerPixel = visibleHeight / std::max(size.y, 1.0f);
        camera.setPos(camera.getPosition() + (-right * io.MouseDelta.x + up * io.MouseDelta.y) * unitsPerPixel);
        ctx.modeLabel = "PAN";
        ctx.hint("Drag", "pan");
    }
}

void Viewport::updateHover(EditorContext& ctx, float dt) {
    m_hoverTimer += dt;
    const bool moved = glm::distance(ctx.mouse, m_lastHoverMouse) > HOVER_MOVE_THRESHOLD;
    if (!ctx.viewportHovered || ctx.modalActive || ImGuizmo::IsUsing()) {
        ctx.hovered = NULL_ENTITY;
        return;
    }
    if (!moved && m_hoverTimer < HOVER_IDLE_INTERVAL) return;
    if (m_hoverTimer < HOVER_MIN_INTERVAL) return;
    m_hoverTimer = 0.0f;
    m_lastHoverMouse = ctx.mouse;
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    const std::optional<RayHit> hit = ctx.engine.pick(ctx.mouse.x, ctx.mouse.y);
    ctx.cursorValid = true;
    ctx.hovered = hit ? hit->entity : NULL_ENTITY;
    if (hit) {
        if (const std::optional<glm::vec3> point = surfacePoint(ctx, ray, *hit)) {
            ctx.cursor = *point;
            ctx.cursorOnSurface = true;
            return;
        }
    }
    ctx.cursorOnSurface = false;
    if (ray.direction.y < -GROUND_RAY_EPSILON) {
        const float distance = -ray.origin.y / ray.direction.y;
        if (distance > 0.0f && distance < GROUND_MAX_DISTANCE) {
            ctx.cursor = ray.origin + ray.direction * distance;
            ctx.cursorOnSurface = true;
            return;
        }
    }
    ctx.cursor = ray.origin + ray.direction * DEFAULT_CURSOR_DISTANCE;
}

void Viewport::handleClicks(EditorContext& ctx, bool mouseFree) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseFree && !io.KeyAlt && !gizmoBusy) {
        m_pressing = true;
        m_boxing = false;
        m_pressAt = ctx.mouse;
    }
    if (!m_pressing) return;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!m_boxing && glm::distance(ctx.mouse, m_pressAt) > CLICK_SLOP) m_boxing = true;
        if (m_boxing) {
            ctx.modeLabel = "SELECT";
            ctx.hint("Shift", "add");
            ctx.hint("Ctrl", "toggle");
        }
        return;
    }
    m_pressing = false;
    if (m_boxing) {
        finishBox(ctx);
        m_boxing = false;
        return;
    }
    EntityHandle picked = iconAt(ctx.mouse);
    if (picked.isNull()) {
        const std::optional<RayHit> hit = ctx.engine.pick(ctx.mouse.x, ctx.mouse.y);
        picked = hit ? hit->entity : NULL_ENTITY;
    }
    const double now = ImGui::GetTime();
    const bool doubleClick = picked == m_lastClicked && !picked.isNull() && now - m_lastClickTime < DOUBLE_CLICK_SECONDS;
    m_lastClickTime = now;
    m_lastClicked = picked;
    if (doubleClick) {
        const glm::vec4 bounds = ctx.world.getWorldBounds(picked);
        flyTo(ctx, bounds.w > 0.0f ? bounds : glm::vec4(ctx.world.getWorldPosition(picked), 1.0f));
        return;
    }
    if (picked.isNull()) {
        if (!io.KeyShift && !io.KeyCtrl) ctx.selection.clear();
        return;
    }
    applySelection(ctx, picked, io);
}

void Viewport::finishBox(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 low = glm::min(m_pressAt, ctx.mouse);
    const glm::vec2 high = glm::max(m_pressAt, ctx.mouse);
    std::vector<EntityHandle> inside;
    const std::vector<EntityHandle> candidates = ctx.world.queryFrustum(ctx.world.camera());
    for (EntityHandle entity : candidates) {
        const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(ctx.world.getWorldPosition(entity));
        if (!screen) continue;
        if (screen->x < low.x || screen->y < low.y || screen->x > high.x || screen->y > high.y) continue;
        inside.push_back(entity);
        if (inside.size() >= BOX_SELECTION_LIMIT) break;
    }
    if (!io.KeyShift && !io.KeyCtrl) ctx.selection.clear();
    for (EntityHandle entity : inside) {
        if (io.KeyCtrl) {
            ctx.selection.toggle(entity);
        } else {
            ctx.selection.add(entity);
        }
    }
    if (inside.size() >= BOX_SELECTION_LIMIT) ctx.toast(std::format("Box selection capped at {} entities", BOX_SELECTION_LIMIT), ToastKind::Warning);
}
