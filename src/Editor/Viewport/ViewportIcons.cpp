module;

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

module Editor.viewport;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr size_t MAX_ICONS = 4096;
constexpr float ICON_PICK_RADIUS = 15.0f;
constexpr float ICON_CULL_MARGIN = 20.0f;
constexpr float ICON_BACKGROUND_RADIUS = 13.0f;
constexpr float ICON_RING_RADIUS = 14.5f;
constexpr int ICON_SEGMENTS = 24;
constexpr float RAY_STEP_ANGLE = 0.785398f;
constexpr float PI_APPROX = 3.14159f;
constexpr float CIRCLE_STEP_ANGLE = 0.261799f;
constexpr int CIRCLE_SEGMENTS = 24;
constexpr float SPOT_GIZMO_MAX_LENGTH = 6.0f;
constexpr float POINT_GIZMO_MAX_RADIUS = 50.0f;
constexpr float CAMERA_FRUSTUM_LENGTH = 1.5f;
constexpr float CAMERA_ASPECT = 16.0f / 9.0f;

ImVec2 toImVec(const glm::vec2& value) { return ImVec2(value.x, value.y); }

void drawIconFrame(ImDrawList* drawList, const ImVec2& center, bool selected, bool primary, bool hot) {
    drawList->AddCircleFilled(center, ICON_BACKGROUND_RADIUS, IM_COL32(18, 20, 26, 170), ICON_SEGMENTS);
    if (selected) {
        drawList->AddCircle(center, ICON_RING_RADIUS, primary ? IM_COL32(255, 184, 72, 255) : IM_COL32(255, 140, 40, 200), ICON_SEGMENTS, 2.0f);
    } else if (hot) {
        drawList->AddCircle(center, ICON_RING_RADIUS, IM_COL32(255, 255, 255, 160), ICON_SEGMENTS, 1.5f);
    }
}

glm::vec2 screenDirection(EditorContext& ctx, EntityHandle entity, const glm::vec2& at) {
    const glm::vec2 fallback(0.0f, 1.0f);
    const glm::vec3 position = ctx.world.getWorldPosition(entity);
    const float scale = std::max(glm::length(position - ctx.world.camera().getPosition()) * 0.1f, 0.1f);
    const std::optional<glm::vec2> tip = ctx.engine.worldToScreen(position + ctx.world.forward(entity) * scale);
    const glm::vec2 direction = tip ? *tip - at : fallback;
    return glm::length(direction) > 1e-3f ? glm::normalize(direction) : fallback;
}

void drawDirectionalGlyph(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    drawList->AddCircleFilled(center, 4.5f, color, 16);
    for (int ray = 0; ray < 8; ++ray) {
        const float angle = static_cast<float>(ray) * RAY_STEP_ANGLE;
        drawList->AddLine(ImVec2(center.x + std::cos(angle) * 6.5f, center.y + std::sin(angle) * 6.5f), ImVec2(center.x + std::cos(angle) * 10.0f, center.y + std::sin(angle) * 10.0f), color, 1.6f);
    }
}

void drawSpotGlyph(ImDrawList* drawList, const glm::vec2& at, const glm::vec2& direction, ImU32 color) {
    const glm::vec2 normal(-direction.y, direction.x);
    const glm::vec2 apex = at - direction * 6.0f;
    const glm::vec2 base = at + direction * 7.0f;
    drawList->AddTriangleFilled(toImVec(apex), ImVec2(base.x + normal.x * 7.0f, base.y + normal.y * 7.0f), ImVec2(base.x - normal.x * 7.0f, base.y - normal.y * 7.0f), color);
    drawList->AddCircleFilled(toImVec(apex), 3.0f, color, 12);
}

void drawPointGlyph(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    drawList->AddCircleFilled(ImVec2(center.x, center.y - 2.0f), 5.5f, color, 16);
    drawList->AddRectFilled(ImVec2(center.x - 3.0f, center.y + 3.5f), ImVec2(center.x + 3.0f, center.y + 7.5f), IM_COL32(190, 195, 205, 255), 1.0f);
    for (int ray = 0; ray < 5; ++ray) {
        const float angle = -PI_APPROX + static_cast<float>(ray) * RAY_STEP_ANGLE;
        drawList->AddLine(ImVec2(center.x + std::cos(angle) * 7.5f, center.y - 2.0f + std::sin(angle) * 7.5f), ImVec2(center.x + std::cos(angle) * 10.0f, center.y - 2.0f + std::sin(angle) * 10.0f), color, 1.4f);
    }
}

void drawSpotCone(EditorContext& ctx, EntityHandle entity, const LightComponent& light, const glm::vec3& position, const glm::vec4& lineColor) {
    const glm::vec3 forward = ctx.world.forward(entity);
    const glm::vec3 side = glm::normalize(glm::cross(forward, std::abs(forward.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(side, forward);
    const float length = std::min(light.range, SPOT_GIZMO_MAX_LENGTH);
    const float radius = length * std::tan(glm::radians(light.outerConeDegrees));
    for (int segment = 0; segment < 8; ++segment) {
        const float startAngle = static_cast<float>(segment) * RAY_STEP_ANGLE;
        const float endAngle = startAngle + RAY_STEP_ANGLE;
        const glm::vec3 rimStart = position + forward * length + (side * std::cos(startAngle) + up * std::sin(startAngle)) * radius;
        const glm::vec3 rimEnd = position + forward * length + (side * std::cos(endAngle) + up * std::sin(endAngle)) * radius;
        ctx.engine.debugLine(rimStart, rimEnd, lineColor, true);
        if (segment % 2 == 0) ctx.engine.debugLine(position, rimStart, lineColor, true);
    }
}

void drawPointRange(EditorContext& ctx, const LightComponent& light, const glm::vec3& position, const glm::vec4& lineColor) {
    const float radius = std::min(light.range, POINT_GIZMO_MAX_RADIUS);
    for (int axis = 0; axis < 3; ++axis) {
        auto pointAt = [&](float angle) {
            glm::vec3 offset(0.0f);
            offset[axis] = std::cos(angle) * radius;
            offset[(axis + 1) % 3] = std::sin(angle) * radius;
            return position + offset;
        };
        for (int segment = 0; segment < CIRCLE_SEGMENTS; ++segment) ctx.engine.debugLine(pointAt(static_cast<float>(segment) * CIRCLE_STEP_ANGLE), pointAt(static_cast<float>(segment + 1) * CIRCLE_STEP_ANGLE), lineColor, true);
    }
}

void drawLightIcon(EditorContext& ctx, ImDrawList* drawList, EntityHandle entity, const LightComponent& light, const glm::vec2& at) {
    const ImVec2 center = toImVec(at);
    const glm::vec3 tint = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f)) * 0.75f + glm::vec3(0.25f);
    const ImU32 color = IM_COL32(static_cast<int>(tint.r * 255.0f), static_cast<int>(tint.g * 255.0f), static_cast<int>(tint.b * 255.0f), 255);
    if (light.type == LightComponent::Type::Directional) {
        drawDirectionalGlyph(drawList, center, color);
    } else if (light.type == LightComponent::Type::Spot) {
        drawSpotGlyph(drawList, at, screenDirection(ctx, entity, at), color);
    } else {
        drawPointGlyph(drawList, center, color);
    }
    if (!ctx.selection.contains(entity) || light.type == LightComponent::Type::Directional) return;
    const glm::vec3 position = ctx.world.getWorldPosition(entity);
    const glm::vec4 lineColor(tint, 0.5f);
    if (light.type == LightComponent::Type::Spot) {
        drawSpotCone(ctx, entity, light, position, lineColor);
    } else {
        drawPointRange(ctx, light, position, lineColor);
    }
}

void drawCameraGlyph(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    drawList->AddRectFilled(ImVec2(center.x - 8.0f, center.y - 4.5f), ImVec2(center.x + 2.0f, center.y + 4.5f), color, 1.5f);
    drawList->AddTriangleFilled(ImVec2(center.x + 2.0f, center.y), ImVec2(center.x + 8.5f, center.y - 5.0f), ImVec2(center.x + 8.5f, center.y + 5.0f), color);
    drawList->AddCircleFilled(ImVec2(center.x - 5.5f, center.y - 7.0f), 2.5f, color, 10);
    drawList->AddCircleFilled(ImVec2(center.x - 0.5f, center.y - 7.0f), 2.5f, color, 10);
}

void drawCameraFrustum(EditorContext& ctx, EntityHandle entity, const CameraComponent& camera) {
    const glm::mat4 matrix = ctx.world.getWorldMatrix(entity);
    const glm::vec3 position(matrix[3]);
    const glm::vec3 forward = ctx.world.forward(entity);
    const glm::vec3 right = glm::normalize(glm::vec3(matrix[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(matrix[1]));
    const float halfHeight = CAMERA_FRUSTUM_LENGTH * std::tan(glm::radians(camera.fov) * 0.5f);
    const float halfWidth = halfHeight * CAMERA_ASPECT;
    const glm::vec3 farCenter = position + forward * CAMERA_FRUSTUM_LENGTH;
    const glm::vec3 corners[4] = {farCenter + right * halfWidth + up * halfHeight, farCenter - right * halfWidth + up * halfHeight, farCenter - right * halfWidth - up * halfHeight, farCenter + right * halfWidth - up * halfHeight};
    const glm::vec4 lineColor(0.85f, 0.9f, 1.0f, 0.7f);
    for (int corner = 0; corner < 4; ++corner) {
        ctx.engine.debugLine(position, corners[corner], lineColor, true);
        ctx.engine.debugLine(corners[corner], corners[(corner + 1) % 4], lineColor, true);
    }
    ctx.engine.debugLine(farCenter + up * halfHeight * 1.15f + right * halfWidth * 0.4f, farCenter + up * halfHeight * 1.6f, lineColor, true);
    ctx.engine.debugLine(farCenter + up * halfHeight * 1.6f, farCenter + up * halfHeight * 1.15f - right * halfWidth * 0.4f, lineColor, true);
}

}

EntityHandle Viewport::iconAt(const glm::vec2& point) const {
    EntityHandle best;
    float bestDistance = ICON_PICK_RADIUS;
    for (const Icon& icon : m_icons) {
        const float distance = glm::distance(icon.at, point);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = icon.entity;
        }
    }
    return best;
}

void Viewport::drawIcons(EditorContext& ctx) {
    m_icons.clear();
    if (ctx.zen) return;
    World& world = ctx.world;
    const glm::vec2 size = ctx.engine.windowSize();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const EntityHandle hoverIcon = ctx.viewportHovered ? iconAt(ctx.mouse) : NULL_ENTITY;
    const EntityHandle primary = ctx.primary();
    auto project = [&](const glm::vec3& point) -> std::optional<glm::vec2> {
        const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(point);
        if (!screen || screen->x < -ICON_CULL_MARGIN || screen->y < -ICON_CULL_MARGIN || screen->x > size.x + ICON_CULL_MARGIN || screen->y > size.y + ICON_CULL_MARGIN) return std::nullopt;
        return screen;
    };
    auto placeIcon = [&](EntityHandle entity) -> std::optional<glm::vec2> {
        if (m_icons.size() >= MAX_ICONS || !world.isVisible(entity)) return std::nullopt;
        const std::optional<glm::vec2> at = project(world.getWorldPosition(entity));
        if (!at) return std::nullopt;
        m_icons.push_back({entity, *at});
        drawIconFrame(drawList, toImVec(*at), ctx.selection.contains(entity), entity == primary, entity == hoverIcon || entity == ctx.hovered);
        return at;
    };
    world.view<LightComponent>([&](EntityHandle entity, LightComponent& light) {
        if (const std::optional<glm::vec2> at = placeIcon(entity)) drawLightIcon(ctx, drawList, entity, light, *at);
    });
    world.view<CameraComponent>([&](EntityHandle entity, CameraComponent& camera) {
        const std::optional<glm::vec2> at = placeIcon(entity);
        if (!at) return;
        const ImU32 color = world.getActiveCamera() == entity ? IM_COL32(120, 220, 140, 255) : IM_COL32(210, 215, 225, 255);
        drawCameraGlyph(drawList, toImVec(*at), color);
        if (ctx.selection.contains(entity)) drawCameraFrustum(ctx, entity, camera);
    });
}
