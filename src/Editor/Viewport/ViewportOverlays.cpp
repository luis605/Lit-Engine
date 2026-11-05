module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

module Editor.viewport;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr int GRID_HALF_LINES = 40;
constexpr int GRID_MAJOR_EVERY = 10;
constexpr float GRID_AXIS_EPSILON = 1e-4f;
constexpr size_t MAX_SELECTION_BOXES = 512;
constexpr float BOX_RADIUS_SCALE = 0.58f;
constexpr float POINT_BOX_RADIUS = 0.25f;
constexpr float HOVER_BOX_DISTANCE_FRACTION = 0.5f;
constexpr float AXIS_WIDGET_SIZE = 96.0f;
constexpr float AXIS_WIDGET_MARGIN = 14.0f;
constexpr float DEFAULT_ORBIT_DISTANCE = 10.0f;
constexpr float ORTHO_LABEL_RIGHT_OFFSET = 124.0f;
constexpr float ORTHO_LABEL_TOP = 18.0f;

bool hasIcon(World& world, EntityHandle entity) { return world.get<LightComponent>(entity) != nullptr || world.get<CameraComponent>(entity) != nullptr; }

void orientTowards(Camera& camera, const glm::vec3& direction) {
    const glm::vec3 unit = glm::normalize(direction);
    camera.setOrientation(glm::degrees(std::atan2(unit.z, unit.x)), glm::degrees(std::asin(std::clamp(unit.y, -1.0f, 1.0f))));
}

bool isMajorLine(float coordinate, float spacing) { return static_cast<long long>(std::round(coordinate / spacing)) % GRID_MAJOR_EVERY == 0; }

bool isIconOnly(EditorContext& ctx, EntityHandle entity, const glm::vec4& bounds) { return bounds.w <= 0.0f && hasIcon(ctx.world, entity); }

glm::vec3 boundsCenter(EditorContext& ctx, EntityHandle entity, const glm::vec4& bounds) { return bounds.w > 0.0f ? glm::vec3(bounds) : ctx.world.getWorldPosition(entity); }

float boundsHalfExtent(const glm::vec4& bounds) { return bounds.w > 0.0f ? bounds.w * BOX_RADIUS_SCALE : POINT_BOX_RADIUS; }

}

void Viewport::drawGrid(EditorContext& ctx) {
    if (!ctx.showGrid) return;
    const glm::vec3 eye = ctx.world.camera().getPosition();
    const float height = std::max(std::abs(eye.y), 1.0f);
    const float spacing = std::pow(10.0f, std::floor(std::log10(height)));
    const float centerX = std::round(eye.x / spacing) * spacing;
    const float centerZ = std::round(eye.z / spacing) * spacing;
    const float extent = spacing * GRID_HALF_LINES;
    const glm::vec4 minor(0.55f, 0.58f, 0.64f, 0.22f);
    const glm::vec4 strong(0.70f, 0.72f, 0.78f, 0.45f);
    for (int line = -GRID_HALF_LINES; line <= GRID_HALF_LINES; ++line) {
        const float x = centerX + static_cast<float>(line) * spacing;
        const float z = centerZ + static_cast<float>(line) * spacing;
        if (std::abs(x) > GRID_AXIS_EPSILON) ctx.engine.debugLine(glm::vec3(x, 0.0f, centerZ - extent), glm::vec3(x, 0.0f, centerZ + extent), isMajorLine(x, spacing) ? strong : minor);
        if (std::abs(z) > GRID_AXIS_EPSILON) ctx.engine.debugLine(glm::vec3(centerX - extent, 0.0f, z), glm::vec3(centerX + extent, 0.0f, z), isMajorLine(z, spacing) ? strong : minor);
    }
    ctx.engine.debugLine(glm::vec3(centerX - extent, 0.0f, 0.0f), glm::vec3(centerX + extent, 0.0f, 0.0f), glm::vec4(0.95f, 0.30f, 0.30f, 0.9f));
    ctx.engine.debugLine(glm::vec3(0.0f, 0.0f, centerZ - extent), glm::vec3(0.0f, 0.0f, centerZ + extent), glm::vec4(0.30f, 0.55f, 1.00f, 0.9f));
}

void Viewport::drawSelection(EditorContext& ctx) {
    size_t drawn = 0;
    const EntityHandle primary = ctx.primary();
    for (EntityHandle entity : ctx.selection.items()) {
        if (drawn++ >= MAX_SELECTION_BOXES) break;
        if (!ctx.world.isAlive(entity)) continue;
        const glm::vec4 bounds = ctx.world.getWorldBounds(entity);
        if (isIconOnly(ctx, entity, bounds)) continue;
        const glm::vec4 color = entity == primary ? glm::vec4(1.0f, 0.72f, 0.28f, 1.0f) : glm::vec4(1.0f, 0.55f, 0.15f, 0.75f);
        ctx.engine.debugBox(boundsCenter(ctx, entity, bounds), glm::vec3(boundsHalfExtent(bounds)), color, true);
    }
    if (ctx.world.isAlive(ctx.hovered) && !ctx.selection.contains(ctx.hovered) && !ctx.flying) {
        const glm::vec4 bounds = ctx.world.getWorldBounds(ctx.hovered);
        if (!isIconOnly(ctx, ctx.hovered, bounds)) {
            const glm::vec3 center = boundsCenter(ctx, ctx.hovered, bounds);
            const float distance = glm::length(center - ctx.world.camera().getPosition());
            if (bounds.w < distance * HOVER_BOX_DISTANCE_FRACTION) ctx.engine.debugBox(center, glm::vec3(boundsHalfExtent(bounds)), glm::vec4(1.0f, 1.0f, 1.0f, 0.35f), true);
        }
    }
    for (EntityHandle entity : ctx.selection.items()) {
        if (!ctx.world.isAlive(entity)) continue;
        const EntityHandle parent = ctx.world.getParent(entity);
        if (!parent.isNull()) ctx.engine.debugLine(ctx.world.getWorldPosition(parent), ctx.world.getWorldPosition(entity), glm::vec4(0.4f, 0.9f, 1.0f, 0.6f), true);
        if (drawn++ >= MAX_SELECTION_BOXES) break;
    }
}

void Viewport::drawAxisWidget(EditorContext& ctx) {
    Camera& camera = ctx.world.camera();
    const glm::vec2 size = ctx.engine.windowSize();
    glm::mat4 view = camera.getViewMatrix();
    const ImVec2 position(size.x - AXIS_WIDGET_SIZE - AXIS_WIDGET_MARGIN, AXIS_WIDGET_MARGIN);
    const float distance = m_orbitDistance > 0.0f ? m_orbitDistance : DEFAULT_ORBIT_DISTANCE;
    const glm::mat4 before = view;
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::ViewManipulate(&view[0][0], distance, position, ImVec2(AXIS_WIDGET_SIZE, AXIS_WIDGET_SIZE), 0x00000000);
    if (std::memcmp(&before, &view, sizeof(glm::mat4)) == 0) return;
    const glm::mat4 inverse = glm::inverse(view);
    camera.setPos(glm::vec3(inverse[3]));
    orientTowards(camera, -glm::vec3(inverse[2]));
    ctx.flight.active = false;
}

void Viewport::drawOverlays(EditorContext& ctx) {
    drawGrid(ctx);
    drawSelection(ctx);
    drawIcons(ctx);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (m_boxing) {
        const glm::vec2 low = glm::min(m_pressAt, ctx.mouse);
        const glm::vec2 high = glm::max(m_pressAt, ctx.mouse);
        drawList->AddRectFilled(ImVec2(low.x, low.y), ImVec2(high.x, high.y), IM_COL32(255, 176, 66, 28));
        drawList->AddRect(ImVec2(low.x, low.y), ImVec2(high.x, high.y), IM_COL32(255, 176, 66, 200), 0.0f, 0, 1.5f);
    }
    if (ctx.playing) {
        const glm::vec2 size = ctx.engine.windowSize();
        drawList->AddRect(ImVec2(1.0f, 1.0f), ImVec2(size.x - 1.0f, size.y - 1.0f), ctx.paused ? IM_COL32(255, 200, 60, 200) : IM_COL32(80, 230, 120, 200), 0.0f, 0, 3.0f);
    }
    if (ctx.zen) return;
    drawAxisWidget(ctx);
    if (!ctx.world.camera().isOrthographic()) return;
    const glm::vec2 size = ctx.engine.windowSize();
    const char* label = "Orthographic  ·  Numpad 5";
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(size.x - ORTHO_LABEL_RIGHT_OFFSET - textSize.x, ORTHO_LABEL_TOP), IM_COL32(255, 200, 120, 220), label);
}
