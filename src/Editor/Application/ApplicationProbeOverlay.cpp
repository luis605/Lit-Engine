module;

#include <imgui.h>

#include <algorithm>

#include "Editor/Application/ApplicationInternal.hpp"

module Editor.application;

import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Editor.context;
import Editor.commands;

namespace {

constexpr const char* kProbeSystem = "reflection volumes";
constexpr glm::vec4 kSelectedProbeColor{0.3f, 0.85f, 1.0f, 1.0f};
constexpr glm::vec4 kIdleProbeColor{0.3f, 0.85f, 1.0f, 0.35f};
constexpr glm::vec4 kBlendColor{1.0f, 0.85f, 0.3f, 0.6f};
constexpr glm::vec4 kSelectedPlaneColor{0.6f, 1.0f, 0.5f, 1.0f};
constexpr glm::vec4 kIdlePlaneColor{0.6f, 1.0f, 0.5f, 0.35f};
constexpr float kNormalArrowLength = 1.0f;

void drawPlane(Engine& engine, World& world, EntityHandle entity, const PlanarReflectionComponent& plane, const glm::vec4& color) {
    const glm::mat4 matrix = world.getWorldMatrix(entity);
    const glm::vec3 center(matrix[3]);
    const glm::vec3 axisU = glm::normalize(glm::vec3(matrix[0])) * plane.halfSize.x;
    const glm::vec3 axisV = glm::normalize(glm::vec3(matrix[2])) * plane.halfSize.y;
    const glm::vec3 normal = glm::normalize(glm::vec3(matrix[1]));
    const glm::vec3 corners[4] = {center - axisU - axisV, center + axisU - axisV, center + axisU + axisV, center - axisU + axisV};
    for (int corner = 0; corner < 4; ++corner) engine.debugLine(corners[corner], corners[(corner + 1) % 4], color, true);
    engine.debugLine(center, center + normal * kNormalArrowLength, color, true);
}

void drawProbe(Engine& engine, World& world, EntityHandle entity, const ReflectionProbeComponent& probe, bool selected) {
    const glm::vec3 center = world.getWorldPosition(entity);
    engine.debugBox(center, probe.halfExtents, selected ? kSelectedProbeColor : kIdleProbeColor, true);
    if (!selected || probe.blendDistance <= 0.0f) return;
    const glm::vec3 inner = glm::max(probe.halfExtents - glm::vec3(probe.blendDistance), glm::vec3(0.0f));
    engine.debugBox(center, inner, kBlendColor, true);
}

}

void Application::registerProbeOverlay() {
    m_engine.addSystem(Phase::PostUpdate, kProbeSystem, [this](World& world, float) {
        const EditorContext& ctx = *m_ctx;
        world.view<ReflectionProbeComponent>([&](EntityHandle entity, ReflectionProbeComponent& probe) {
            drawProbe(m_engine, world, entity, probe, ctx.selection.contains(entity));
        });
        world.view<PlanarReflectionComponent>([&](EntityHandle entity, PlanarReflectionComponent& plane) {
            drawPlane(m_engine, world, entity, plane, ctx.selection.contains(entity) ? kSelectedPlaneColor : kIdlePlaneColor);
        });
    }, true);
    addCommand("view.reflectionVolumes", "Show reflection probe and planar volumes", "View", {}, [this] {
        m_engine.setSystemEnabled(kProbeSystem, !m_engine.isSystemEnabled(kProbeSystem));
    }).checked = [this] { return m_engine.isSystemEnabled(kProbeSystem); };
}
