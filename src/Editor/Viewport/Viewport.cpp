module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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

constexpr float kClickSlop = 5.0f;

glm::vec3 cameraRight(const Camera& camera) {
    return glm::normalize(glm::cross(cameraForward(camera), glm::vec3(0.0f, 1.0f, 0.0f)));
}

void orientTowards(Camera& camera, const glm::vec3& direction) {
    const glm::vec3 d = glm::normalize(direction);
    camera.setOrientation(glm::degrees(std::atan2(d.z, d.x)), glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))));
}

}  // namespace

glm::vec3 Viewport::pivotFor(EditorContext& ctx) const {
    const auto top = ctx.topLevelSelection();
    if (!top.empty()) {
        glm::vec3 sum(0.0f);
        for (EntityHandle e : top) sum += ctx.world.getWorldPosition(e);
        return sum / static_cast<float>(top.size());
    }
    if (ctx.cursorOnSurface) return ctx.cursor;
    const Camera& camera = ctx.world.camera();
    return camera.getPosition() + cameraForward(camera) * 10.0f;
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
    ctx.mouse = glm::vec2(io.MousePos.x, io.MousePos.y);
    ctx.viewportHovered = mouseFree;
    updateFlight(ctx, dt);
    updateFly(ctx, dt);
    if (!ctx.flying) updateOrbitPan(ctx);
    if (!ctx.flying && !m_orbiting && !m_panning) {
        updateHover(ctx, dt);
        if (!ctx.modalActive) handleClicks(ctx, mouseFree);
    }

    if (mouseFree && !ctx.flying && !m_orbiting && io.MouseWheel != 0.0f && !ctx.modalActive) {
        Camera& camera = ctx.world.camera();
        const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
        const float distance = ctx.cursorOnSurface ? glm::length(ctx.cursor - camera.getPosition()) : ctx.flySpeed * 2.0f;
        const float step = std::clamp(distance * 0.15f, 0.05f, 5000.0f) * io.MouseWheel * (io.KeyShift ? 3.0f : 1.0f);
        camera.setPos(camera.getPosition() + ray.direction * step);
        ctx.flight.active = false;
    }
}

void Viewport::updateFly(EditorContext& ctx, float dt) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!ctx.flying) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ctx.viewportHovered && !ctx.modalActive) {
            ctx.flying = true;
            ctx.flight.active = false;
            ctx.engine.setMouseCaptured(true);
        }
        return;
    }
    if (!rightDown) {
        ctx.flying = false;
        ctx.engine.setMouseCaptured(false);
        return;
    }
    Camera& camera = ctx.world.camera();
    const glm::vec2 delta = InputManager::GetMouseDelta();
    camera.processMouseMovement(delta.x, -delta.y);
    if (io.MouseWheel != 0.0f) ctx.flySpeed = std::clamp(ctx.flySpeed * std::pow(1.2f, io.MouseWheel), 0.1f, 5000.0f);
    float speed = ctx.flySpeed * (io.KeyShift ? 4.0f : 1.0f) * (io.KeyCtrl ? 0.25f : 1.0f) * dt;
    const glm::vec3 forward = cameraForward(camera);
    const glm::vec3 right = cameraRight(camera);
    glm::vec3 move(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
    if (ImGui::IsKeyDown(ImGuiKey_E)) move += glm::vec3(0.0f, 1.0f, 0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::dot(move, move) > 0.0f) camera.setPos(camera.getPosition() + glm::normalize(move) * speed);
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
            m_orbitDistance = std::max(glm::length(camera.getPosition() - m_orbitPivot), 0.5f);
            m_orbiting = orbitStart;
            m_panning = panStart;
            ctx.flight.active = false;
        }
    }
    if (m_orbiting) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_orbiting = false;
            return;
        }
        camera.processMouseMovement(io.MouseDelta.x * 2.5f, -io.MouseDelta.y * 2.5f);
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
        const float unitsPerPixel = 2.0f * m_orbitDistance * std::tan(glm::radians(camera.getFov()) * 0.5f) / std::max(size.y, 1.0f);
        camera.setPos(camera.getPosition() + (-right * io.MouseDelta.x + up * io.MouseDelta.y) * unitsPerPixel);
        ctx.modeLabel = "PAN";
        ctx.hint("Drag", "pan");
    }
}

void Viewport::updateHover(EditorContext& ctx, float dt) {
    m_hoverTimer += dt;
    const bool moved = glm::distance(ctx.mouse, m_lastHoverMouse) > 0.5f;
    if (!ctx.viewportHovered || ctx.modalActive || ImGuizmo::IsUsing()) {
        ctx.hovered = NULL_ENTITY;
        return;
    }
    if (!moved && m_hoverTimer < 0.25f) return;
    if (m_hoverTimer < 0.04f) return;
    m_hoverTimer = 0.0f;
    m_lastHoverMouse = ctx.mouse;
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    const std::optional<RayHit> hit = ctx.engine.pick(ctx.mouse.x, ctx.mouse.y);
    if (hit) {
        ctx.hovered = hit->entity;
        ctx.cursor = ray.origin + ray.direction * hit->distance;
        ctx.cursorOnSurface = true;
        return;
    }
    ctx.hovered = NULL_ENTITY;
    ctx.cursorOnSurface = false;
    if (ray.direction.y < -1e-4f) {
        const float t = -ray.origin.y / ray.direction.y;
        if (t > 0.0f && t < 5000.0f) {
            ctx.cursor = ray.origin + ray.direction * t;
            return;
        }
    }
    ctx.cursor = ray.origin + ray.direction * 10.0f;
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
        if (!m_boxing && glm::distance(ctx.mouse, m_pressAt) > kClickSlop) m_boxing = true;
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
    const std::optional<RayHit> hit = ctx.engine.pick(ctx.mouse.x, ctx.mouse.y);
    const EntityHandle picked = hit ? hit->entity : NULL_ENTITY;
    const double now = ImGui::GetTime();
    const bool doubleClick = picked == m_lastClicked && !picked.isNull() && now - m_lastClickTime < 0.35;
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
    if (io.KeyCtrl) {
        ctx.selection.toggle(picked);
    } else if (io.KeyShift) {
        ctx.selection.remove(picked);
        ctx.selection.add(picked);
    } else {
        ctx.selection.set(picked);
    }
}

void Viewport::finishBox(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 lo = glm::min(m_pressAt, ctx.mouse);
    const glm::vec2 hi = glm::max(m_pressAt, ctx.mouse);
    constexpr size_t kLimit = 100000;
    std::vector<EntityHandle> inside;
    const std::vector<EntityHandle> candidates = ctx.world.queryFrustum(ctx.world.camera());
    for (EntityHandle e : candidates) {
        const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(ctx.world.getWorldPosition(e));
        if (!screen) continue;
        if (screen->x < lo.x || screen->y < lo.y || screen->x > hi.x || screen->y > hi.y) continue;
        inside.push_back(e);
        if (inside.size() >= kLimit) break;
    }
    if (!io.KeyShift && !io.KeyCtrl) ctx.selection.clear();
    for (EntityHandle e : inside) {
        if (io.KeyCtrl) {
            ctx.selection.toggle(e);
        } else {
            ctx.selection.add(e);
        }
    }
    if (inside.size() >= kLimit) ctx.toast(std::format("Box selection capped at {} entities", kLimit), ToastKind::Warning);
}

void Viewport::drawGrid(EditorContext& ctx) {
    if (!ctx.showGrid) return;
    const Camera& camera = ctx.world.camera();
    const glm::vec3 eye = camera.getPosition();
    const float height = std::max(std::abs(eye.y), 1.0f);
    const float spacing = std::pow(10.0f, std::floor(std::log10(height)));
    constexpr int kHalf = 40;
    const float cx = std::round(eye.x / spacing) * spacing;
    const float cz = std::round(eye.z / spacing) * spacing;
    const float extent = spacing * kHalf;
    for (int i = -kHalf; i <= kHalf; ++i) {
        const float x = cx + static_cast<float>(i) * spacing;
        const float z = cz + static_cast<float>(i) * spacing;
        const bool major = static_cast<long long>(std::round(x / spacing)) % 10 == 0;
        const bool majorZ = static_cast<long long>(std::round(z / spacing)) % 10 == 0;
        const glm::vec4 minor(0.55f, 0.58f, 0.64f, 0.22f);
        const glm::vec4 strong(0.70f, 0.72f, 0.78f, 0.45f);
        if (std::abs(x) > 1e-4f) ctx.engine.debugLine(glm::vec3(x, 0.0f, cz - extent), glm::vec3(x, 0.0f, cz + extent), major ? strong : minor);
        if (std::abs(z) > 1e-4f) ctx.engine.debugLine(glm::vec3(cx - extent, 0.0f, z), glm::vec3(cx + extent, 0.0f, z), majorZ ? strong : minor);
    }
    ctx.engine.debugLine(glm::vec3(cx - extent, 0.0f, 0.0f), glm::vec3(cx + extent, 0.0f, 0.0f), glm::vec4(0.95f, 0.30f, 0.30f, 0.9f));
    ctx.engine.debugLine(glm::vec3(0.0f, 0.0f, cz - extent), glm::vec3(0.0f, 0.0f, cz + extent), glm::vec4(0.30f, 0.55f, 1.00f, 0.9f));
}

void Viewport::drawSelection(EditorContext& ctx) {
    constexpr size_t kMaxBoxes = 512;
    size_t drawn = 0;
    const EntityHandle primary = ctx.primary();
    for (EntityHandle e : ctx.selection.items()) {
        if (drawn++ >= kMaxBoxes) break;
        if (!ctx.world.isAlive(e)) continue;
        const glm::vec4 b = ctx.world.getWorldBounds(e);
        const glm::vec3 center = b.w > 0.0f ? glm::vec3(b) : ctx.world.getWorldPosition(e);
        const float r = b.w > 0.0f ? b.w * 0.58f : 0.25f;
        const glm::vec4 color = e == primary ? glm::vec4(1.0f, 0.72f, 0.28f, 1.0f) : glm::vec4(1.0f, 0.55f, 0.15f, 0.75f);
        ctx.engine.debugBox(center, glm::vec3(r), color, true);
    }
    if (ctx.world.isAlive(ctx.hovered) && !ctx.selection.contains(ctx.hovered) && !ctx.flying) {
        const glm::vec4 b = ctx.world.getWorldBounds(ctx.hovered);
        const glm::vec3 center = b.w > 0.0f ? glm::vec3(b) : ctx.world.getWorldPosition(ctx.hovered);
        const float distance = glm::length(center - ctx.world.camera().getPosition());
        if (b.w < distance * 0.5f) ctx.engine.debugBox(center, glm::vec3(b.w > 0.0f ? b.w * 0.58f : 0.25f), glm::vec4(1.0f, 1.0f, 1.0f, 0.35f), true);
    }
    for (EntityHandle e : ctx.selection.items()) {
        if (!ctx.world.isAlive(e)) continue;
        const EntityHandle parent = ctx.world.getParent(e);
        if (!parent.isNull()) ctx.engine.debugLine(ctx.world.getWorldPosition(parent), ctx.world.getWorldPosition(e), glm::vec4(0.4f, 0.9f, 1.0f, 0.6f), true);
        if (drawn++ >= kMaxBoxes) break;
    }
}

void Viewport::drawAxisWidget(EditorContext& ctx) {
    Camera& camera = ctx.world.camera();
    const glm::vec2 size = ctx.engine.windowSize();
    glm::mat4 view = camera.getViewMatrix();
    const float widget = 96.0f;
    const ImVec2 pos(size.x - widget - 14.0f, 14.0f);
    const float distance = m_orbitDistance > 0.0f ? m_orbitDistance : 10.0f;
    const glm::mat4 before = view;
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::ViewManipulate(&view[0][0], distance, pos, ImVec2(widget, widget), 0x00000000);
    if (std::memcmp(&before, &view, sizeof(glm::mat4)) != 0) {
        const glm::mat4 inverse = glm::inverse(view);
        camera.setPos(glm::vec3(inverse[3]));
        orientTowards(camera, -glm::vec3(inverse[2]));
        ctx.flight.active = false;
    }
}

void Viewport::drawOverlays(EditorContext& ctx) {
    drawGrid(ctx);
    drawSelection(ctx);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (m_boxing) {
        const glm::vec2 lo = glm::min(m_pressAt, ctx.mouse);
        const glm::vec2 hi = glm::max(m_pressAt, ctx.mouse);
        draw->AddRectFilled(ImVec2(lo.x, lo.y), ImVec2(hi.x, hi.y), IM_COL32(255, 176, 66, 28));
        draw->AddRect(ImVec2(lo.x, lo.y), ImVec2(hi.x, hi.y), IM_COL32(255, 176, 66, 200), 0.0f, 0, 1.5f);
    }
    if (ctx.playing) {
        const glm::vec2 size = ctx.engine.windowSize();
        draw->AddRect(ImVec2(1.0f, 1.0f), ImVec2(size.x - 1.0f, size.y - 1.0f), ctx.paused ? IM_COL32(255, 200, 60, 200) : IM_COL32(80, 230, 120, 200), 0.0f, 0, 3.0f);
    }
    if (!ctx.zen) drawAxisWidget(ctx);
}
