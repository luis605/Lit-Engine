module;

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.camera;
import Engine.glm;

namespace {

constexpr float SCREEN_MARGIN = 8.0f;
constexpr float BOTTOM_MARGIN = 40.0f;
constexpr float STATS_COLUMN_WIDTH = 330.0f;
constexpr float OFFSCREEN_TOLERANCE = 200.0f;
constexpr float MIN_TARGET_RADIUS = 24.0f;
constexpr float MIN_NEIGHBOR_RADIUS = 16.0f;
constexpr float MAX_NEIGHBOR_RADIUS_FRACTION = 0.3f;
constexpr float CARD_GAP = 28.0f;
constexpr size_t MAX_NEIGHBORS = 64;
constexpr float SELF_OVERLAP_WEIGHT = 50.0f;
constexpr float OVERLAP_SCORE_SCALE = 1000.0f;
constexpr float DOCK_PENALTY = 120.0f;
constexpr float DISTANCE_WEIGHT = 0.05f;
constexpr float STICKY_DISTANCE = 40.0f;
constexpr float STICKY_BONUS = 25.0f;
constexpr float RECT_TOLERANCE = 1.0f;
constexpr float MIN_DEPTH = 0.1f;

struct Candidate {
    glm::vec2 at;
    int side;
    bool dock;
};

glm::vec3 cameraUpVector(const Camera& camera) {
    const glm::vec3 forward = cameraForward(camera);
    const glm::vec3 cross = glm::cross(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)), forward);
    if (glm::dot(cross, cross) < 1e-12f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return glm::normalize(cross);
}

float overlapArea(const glm::vec4& first, const glm::vec4& second) {
    return std::max(0.0f, std::min(first.z, second.z) - std::max(first.x, second.x)) * std::max(0.0f, std::min(first.w, second.w) - std::max(first.y, second.y));
}

glm::vec3 boundsAnchor(const glm::vec4& bounds, const glm::vec3& fallback) { return bounds.w > 0.0f ? glm::vec3(bounds) : fallback; }

float projectedRadius(EditorContext& ctx, const glm::vec3& anchor, float worldRadius, const glm::vec2& screen, const glm::vec3& cameraUp) {
    if (worldRadius <= 0.0f) return 0.0f;
    const std::optional<glm::vec2> edge = ctx.engine.worldToScreen(anchor + cameraUp * worldRadius);
    return edge ? glm::length(*edge - screen) : 0.0f;
}

float worldUnitsPerPixel(const Camera& camera, const glm::vec3& anchor, float screenHeight) {
    const float depth = std::max(glm::dot(anchor - camera.getPosition(), cameraForward(camera)), MIN_DEPTH);
    const float visibleHeight = camera.isOrthographic() ? camera.getOrthoHeight() : 2.0f * depth * std::tan(glm::radians(camera.getFov()) * 0.5f);
    return visibleHeight / std::max(screenHeight, 1.0f);
}

std::vector<glm::vec4> collectNeighborRects(EditorContext& ctx, EntityHandle entity, const glm::vec3& anchor, float searchRadiusPixels, const glm::vec3& cameraUp) {
    const glm::vec2 windowSize = ctx.engine.windowSize();
    const float searchRadius = searchRadiusPixels * worldUnitsPerPixel(ctx.world.camera(), anchor, windowSize.y);
    const std::vector<EntityHandle> around = ctx.world.overlapSphere(anchor, searchRadius);
    std::vector<glm::vec4> neighbors;
    neighbors.reserve(std::min(around.size(), MAX_NEIGHBORS));
    for (size_t index = 0; index < around.size() && neighbors.size() < MAX_NEIGHBORS; ++index) {
        const EntityHandle other = around[index];
        if (other == entity || !ctx.world.isVisible(other)) continue;
        const glm::vec4 bounds = ctx.world.getWorldBounds(other);
        const glm::vec3 center = boundsAnchor(bounds, ctx.world.getWorldPosition(other));
        const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(center);
        if (!screen) continue;
        const float radius = std::max(projectedRadius(ctx, center, bounds.w, *screen, cameraUp), MIN_NEIGHBOR_RADIUS);
        if (radius > windowSize.y * MAX_NEIGHBOR_RADIUS_FRACTION) continue;
        neighbors.emplace_back(screen->x - radius, screen->y - radius, screen->x + radius, screen->y + radius);
    }
    return neighbors;
}

}

glm::vec2 Inspector::placeCard(EditorContext& ctx, EntityHandle entity, float width, float height) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float left = viewport->WorkPos.x + SCREEN_MARGIN;
    float right = viewport->WorkPos.x + viewport->WorkSize.x - SCREEN_MARGIN;
    const float middle = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
    for (const glm::vec4& panel : ctx.panels) {
        if (panel.x + panel.z * 0.5f < middle) {
            left = std::max(left, panel.x + panel.z + SCREEN_MARGIN);
        } else {
            right = std::min(right, panel.x - SCREEN_MARGIN);
        }
    }
    const float top = viewport->WorkPos.y + SCREEN_MARGIN;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - BOTTOM_MARGIN;
    const float statsTop = ctx.rightColumnTop;
    const float statsLeft = viewport->WorkPos.x + viewport->WorkSize.x - STATS_COLUMN_WIDTH;
    auto clampY = [&](float x, float y) {
        const float minY = x + width > statsLeft ? statsTop : top;
        return std::clamp(y, minY, std::max(minY, bottom - height));
    };
    const glm::vec2 docked(right - width, clampY(right - width, top));
    const glm::vec4 bounds = ctx.world.getWorldBounds(entity);
    const glm::vec3 anchor = boundsAnchor(bounds, ctx.world.getWorldPosition(entity));
    const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(anchor);
    m_leader.reset();
    if (!screen || screen->x < left - OFFSCREEN_TOLERANCE || screen->x > right + OFFSCREEN_TOLERANCE || screen->y < top - OFFSCREEN_TOLERANCE || screen->y > bottom + OFFSCREEN_TOLERANCE) return docked;

    const glm::vec3 cameraUp = cameraUpVector(ctx.world.camera());
    const float radius = std::max(MIN_TARGET_RADIUS, projectedRadius(ctx, anchor, bounds.w, *screen, cameraUp));
    const glm::vec4 self(screen->x - radius - CARD_GAP, screen->y - radius - CARD_GAP, screen->x + radius + CARD_GAP, screen->y + radius + CARD_GAP);
    const std::vector<glm::vec4> neighbors = collectNeighborRects(ctx, entity, anchor, width + radius + CARD_GAP * 2.0f, cameraUp);

    std::vector<Candidate> candidates;
    candidates.reserve(12);
    for (const float fraction : {0.3f, 0.0f, 0.7f, 1.0f}) {
        candidates.push_back({{self.z, clampY(self.z, screen->y - height * fraction)}, 1, false});
        candidates.push_back({{self.x - width, clampY(self.x - width, screen->y - height * fraction)}, -1, false});
    }
    const float centeredX = std::clamp(screen->x - width * 0.5f, left, std::max(left, right - width));
    candidates.push_back({{centeredX, self.w}, 0, false});
    candidates.push_back({{centeredX, self.y - height}, 0, false});
    candidates.push_back({docked, 2, true});
    candidates.push_back({{left, clampY(left, top)}, -2, true});

    const float area = std::max(width * height, 1.0f);
    float bestScore = std::numeric_limits<float>::max();
    Candidate best = candidates[candidates.size() - 2];
    for (const Candidate& candidate : candidates) {
        const glm::vec4 rect(candidate.at.x, candidate.at.y, candidate.at.x + width, candidate.at.y + height);
        if (rect.x < left - RECT_TOLERANCE || rect.z > right + RECT_TOLERANCE || rect.y < top - RECT_TOLERANCE || rect.w > bottom + RECT_TOLERANCE) continue;
        float covered = overlapArea(rect, self) * SELF_OVERLAP_WEIGHT;
        for (const glm::vec4& neighbor : neighbors) covered += overlapArea(rect, neighbor);
        const float placementCost = candidate.dock ? DOCK_PENALTY : glm::distance(candidate.at + glm::vec2(width, height) * 0.5f, *screen) * DISTANCE_WEIGHT;
        float score = covered / area * OVERLAP_SCORE_SCALE + placementCost;
        if (glm::distance(candidate.at, m_lastTarget) < STICKY_DISTANCE) score -= STICKY_BONUS;
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    if (best.side == 1 || best.side == -1) m_side = best.side;
    m_lastTarget = best.at;
    if (best.dock) m_leader = *screen;
    return best.at;
}
