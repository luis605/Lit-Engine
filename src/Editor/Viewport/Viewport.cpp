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

bool hasIcon(World& world, EntityHandle e) { return world.get<LightComponent>(e) != nullptr || world.get<CameraComponent>(e) != nullptr; }

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
        Camera& camera = ctx.world.camera();
        const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
        if (camera.isOrthographic()) {
            const float factor = std::pow(io.KeyShift ? 0.7f : 0.87f, io.MouseWheel);
            const glm::vec3 forward = cameraForward(camera);
            glm::vec3 offset = ray.origin - camera.getPosition();
            offset -= forward * glm::dot(offset, forward);
            camera.setOrthoHeight(std::clamp(camera.getOrthoHeight() * factor, 0.01f, 100000.0f));
            camera.setPos(camera.getPosition() + offset * (1.0f - factor));
        } else {
            const float distance = ctx.cursorOnSurface ? glm::length(ctx.cursor - camera.getPosition()) : ctx.flySpeed * 2.0f;
            const float step = std::clamp(distance * 0.15f, 0.05f, 5000.0f) * io.MouseWheel * (io.KeyShift ? 3.0f : 1.0f);
            camera.setPos(camera.getPosition() + ray.direction * step);
        }
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
            ctx.world.camera().setOrthographic(false);
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
        const float unitsPerPixel = (camera.isOrthographic() ? camera.getOrthoHeight() : 2.0f * m_orbitDistance * std::tan(glm::radians(camera.getFov()) * 0.5f)) / std::max(size.y, 1.0f);
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
    if (ray.direction.y < -1e-4f) {
        const float t = -ray.origin.y / ray.direction.y;
        if (t > 0.0f && t < 5000.0f) {
            ctx.cursor = ray.origin + ray.direction * t;
            ctx.cursorOnSurface = true;
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
    EntityHandle picked = iconAt(ctx.mouse);
    if (picked.isNull()) {
        const std::optional<RayHit> hit = ctx.engine.pick(ctx.mouse.x, ctx.mouse.y);
        picked = hit ? hit->entity : NULL_ENTITY;
    }
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
        if (b.w <= 0.0f && hasIcon(ctx.world, e)) continue;
        const glm::vec3 center = b.w > 0.0f ? glm::vec3(b) : ctx.world.getWorldPosition(e);
        const float r = b.w > 0.0f ? b.w * 0.58f : 0.25f;
        const glm::vec4 color = e == primary ? glm::vec4(1.0f, 0.72f, 0.28f, 1.0f) : glm::vec4(1.0f, 0.55f, 0.15f, 0.75f);
        ctx.engine.debugBox(center, glm::vec3(r), color, true);
    }
    if (ctx.world.isAlive(ctx.hovered) && !ctx.selection.contains(ctx.hovered) && !ctx.flying && !(ctx.world.getWorldBounds(ctx.hovered).w <= 0.0f && hasIcon(ctx.world, ctx.hovered))) {
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

EntityHandle Viewport::iconAt(const glm::vec2& point) const {
    EntityHandle best;
    float bestDistance = 15.0f;
    for (const Icon& icon : m_icons) {
        const float d = glm::distance(icon.at, point);
        if (d < bestDistance) {
            bestDistance = d;
            best = icon.entity;
        }
    }
    return best;
}

void Viewport::drawIcons(EditorContext& ctx) {
    m_icons.clear();
    if (ctx.zen) return;
    constexpr size_t kMaxIcons = 4096;
    World& world = ctx.world;
    const glm::vec2 size = ctx.engine.windowSize();
    const glm::vec3 eye = world.camera().getPosition();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const EntityHandle hoverIcon = ctx.viewportHovered ? iconAt(ctx.mouse) : NULL_ENTITY;
    auto project = [&](const glm::vec3& p) -> std::optional<glm::vec2> {
        const std::optional<glm::vec2> s = ctx.engine.worldToScreen(p);
        if (!s || s->x < -20.0f || s->y < -20.0f || s->x > size.x + 20.0f || s->y > size.y + 20.0f) return std::nullopt;
        return s;
    };
    auto frame = [&](EntityHandle e, const ImVec2& c) {
        const bool selected = ctx.selection.contains(e);
        const bool hot = e == hoverIcon || e == ctx.hovered;
        draw->AddCircleFilled(c, 13.0f, IM_COL32(18, 20, 26, 170), 24);
        if (selected) {
            draw->AddCircle(c, 14.5f, e == ctx.primary() ? IM_COL32(255, 184, 72, 255) : IM_COL32(255, 140, 40, 200), 24, 2.0f);
        } else if (hot) {
            draw->AddCircle(c, 14.5f, IM_COL32(255, 255, 255, 160), 24, 1.5f);
        }
    };
    auto screenDirection = [&](EntityHandle e, const glm::vec2& at) {
        const glm::vec3 p = world.getWorldPosition(e);
        const float scale = std::max(glm::length(p - eye) * 0.1f, 0.1f);
        const std::optional<glm::vec2> tip = ctx.engine.worldToScreen(p + world.forward(e) * scale);
        const glm::vec2 d = tip ? *tip - at : glm::vec2(0.0f, 1.0f);
        return glm::length(d) > 1e-3f ? glm::normalize(d) : glm::vec2(0.0f, 1.0f);
    };
    world.view<LightComponent>([&](EntityHandle e, LightComponent& light) {
        if (m_icons.size() >= kMaxIcons || !world.isVisible(e)) return;
        const std::optional<glm::vec2> at = project(world.getWorldPosition(e));
        if (!at) return;
        m_icons.push_back({e, *at});
        const ImVec2 c(at->x, at->y);
        frame(e, c);
        const glm::vec3 tint = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f)) * 0.75f + glm::vec3(0.25f);
        const ImU32 color = IM_COL32(static_cast<int>(tint.r * 255.0f), static_cast<int>(tint.g * 255.0f), static_cast<int>(tint.b * 255.0f), 255);
        if (light.type == LightComponent::Type::Directional) {
            draw->AddCircleFilled(c, 4.5f, color, 16);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 0.785398f;
                draw->AddLine(ImVec2(c.x + std::cos(a) * 6.5f, c.y + std::sin(a) * 6.5f), ImVec2(c.x + std::cos(a) * 10.0f, c.y + std::sin(a) * 10.0f), color, 1.6f);
            }
        } else if (light.type == LightComponent::Type::Spot) {
            const glm::vec2 d = screenDirection(e, *at);
            const glm::vec2 n(-d.y, d.x);
            const glm::vec2 apex = *at - d * 6.0f;
            const glm::vec2 base = *at + d * 7.0f;
            draw->AddTriangleFilled(ImVec2(apex.x, apex.y), ImVec2(base.x + n.x * 7.0f, base.y + n.y * 7.0f), ImVec2(base.x - n.x * 7.0f, base.y - n.y * 7.0f), color);
            draw->AddCircleFilled(ImVec2(apex.x, apex.y), 3.0f, color, 12);
        } else {
            draw->AddCircleFilled(ImVec2(c.x, c.y - 2.0f), 5.5f, color, 16);
            draw->AddRectFilled(ImVec2(c.x - 3.0f, c.y + 3.5f), ImVec2(c.x + 3.0f, c.y + 7.5f), IM_COL32(190, 195, 205, 255), 1.0f);
            for (int i = 0; i < 5; ++i) {
                const float a = -3.14159f + static_cast<float>(i) * 0.785398f;
                draw->AddLine(ImVec2(c.x + std::cos(a) * 7.5f, c.y - 2.0f + std::sin(a) * 7.5f), ImVec2(c.x + std::cos(a) * 10.0f, c.y - 2.0f + std::sin(a) * 10.0f), color, 1.4f);
            }
        }
        if (!ctx.selection.contains(e) || light.type == LightComponent::Type::Directional) return;
        const glm::vec3 p = world.getWorldPosition(e);
        const glm::vec4 lineColor(tint, 0.5f);
        if (light.type == LightComponent::Type::Spot) {
            const glm::vec3 f = world.forward(e);
            const glm::vec3 side = glm::normalize(glm::cross(f, std::abs(f.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::cross(side, f);
            const float length = std::min(light.range, 6.0f);
            const float radius = length * std::tan(glm::radians(light.outerConeDegrees));
            for (int i = 0; i < 8; ++i) {
                const float a0 = static_cast<float>(i) * 0.785398f;
                const float a1 = a0 + 0.785398f;
                const glm::vec3 r0 = p + f * length + (side * std::cos(a0) + up * std::sin(a0)) * radius;
                const glm::vec3 r1 = p + f * length + (side * std::cos(a1) + up * std::sin(a1)) * radius;
                ctx.engine.debugLine(r0, r1, lineColor, true);
                if (i % 2 == 0) ctx.engine.debugLine(p, r0, lineColor, true);
            }
            return;
        }
        const float r = std::min(light.range, 50.0f);
        for (int axis = 0; axis < 3; ++axis) {
            auto point = [&](float a) {
                glm::vec3 v(0.0f);
                v[axis] = std::cos(a) * r;
                v[(axis + 1) % 3] = std::sin(a) * r;
                return p + v;
            };
            for (int i = 0; i < 24; ++i) ctx.engine.debugLine(point(static_cast<float>(i) * 0.261799f), point(static_cast<float>(i + 1) * 0.261799f), lineColor, true);
        }
    });
    world.view<CameraComponent>([&](EntityHandle e, CameraComponent& camera) {
        if (m_icons.size() >= kMaxIcons || !world.isVisible(e)) return;
        const std::optional<glm::vec2> at = project(world.getWorldPosition(e));
        if (!at) return;
        m_icons.push_back({e, *at});
        const ImVec2 c(at->x, at->y);
        frame(e, c);
        const ImU32 color = world.getActiveCamera() == e ? IM_COL32(120, 220, 140, 255) : IM_COL32(210, 215, 225, 255);
        draw->AddRectFilled(ImVec2(c.x - 8.0f, c.y - 4.5f), ImVec2(c.x + 2.0f, c.y + 4.5f), color, 1.5f);
        draw->AddTriangleFilled(ImVec2(c.x + 2.0f, c.y), ImVec2(c.x + 8.5f, c.y - 5.0f), ImVec2(c.x + 8.5f, c.y + 5.0f), color);
        draw->AddCircleFilled(ImVec2(c.x - 5.5f, c.y - 7.0f), 2.5f, color, 10);
        draw->AddCircleFilled(ImVec2(c.x - 0.5f, c.y - 7.0f), 2.5f, color, 10);
        if (!ctx.selection.contains(e)) return;
        const glm::mat4 m = world.getWorldMatrix(e);
        const glm::vec3 p(m[3]);
        const glm::vec3 f = world.forward(e);
        const glm::vec3 right = glm::normalize(glm::vec3(m[0]));
        const glm::vec3 up = glm::normalize(glm::vec3(m[1]));
        const float length = 1.5f;
        const float h = length * std::tan(glm::radians(camera.fov) * 0.5f);
        const float w = h * 16.0f / 9.0f;
        const glm::vec3 far = p + f * length;
        const glm::vec3 corners[4] = {far + right * w + up * h, far - right * w + up * h, far - right * w - up * h, far + right * w - up * h};
        const glm::vec4 lineColor(0.85f, 0.9f, 1.0f, 0.7f);
        for (int i = 0; i < 4; ++i) {
            ctx.engine.debugLine(p, corners[i], lineColor, true);
            ctx.engine.debugLine(corners[i], corners[(i + 1) % 4], lineColor, true);
        }
        ctx.engine.debugLine(far + up * h * 1.15f + right * w * 0.4f, far + up * h * 1.6f, lineColor, true);
        ctx.engine.debugLine(far + up * h * 1.6f, far + up * h * 1.15f - right * w * 0.4f, lineColor, true);
    });
}

void Viewport::drawOverlays(EditorContext& ctx) {
    drawGrid(ctx);
    drawSelection(ctx);
    drawIcons(ctx);
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
    if (!ctx.zen) {
        drawAxisWidget(ctx);
        if (ctx.world.camera().isOrthographic()) {
            const glm::vec2 size = ctx.engine.windowSize();
            const char* label = "Orthographic  ·  Numpad 5";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(size.x - 124.0f - textSize.x, 18.0f), IM_COL32(255, 200, 120, 220), label);
        }
    }
}
