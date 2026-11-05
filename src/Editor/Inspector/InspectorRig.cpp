module;

#include <imgui.h>

#include <algorithm>
#include <string>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.IK;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr const char* kChainName = "IkChain";
constexpr const char* kWalkerName = "Walker";
constexpr const char* kDebugSystem = "ik debug";
constexpr int kMaxIterations = 64;
constexpr float kMaxSpeed = 50.0f;
constexpr float kMaxLength = 20.0f;
constexpr float kMaxTiltDegrees = 90.0f;
constexpr float kMaxSwingTime = 5.0f;

void drawDebugToggle(EditorContext& ctx) {
    bool debug = ctx.engine.isSystemEnabled(kDebugSystem);
    if (ImGui::Checkbox("Debug draw", &debug)) ctx.engine.setSystemEnabled(kDebugSystem, debug);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bones, targets and feet in the viewport (palette: ik.debug)");
}

}

void Inspector::drawIkChainComponent(EditorContext& ctx, EntityHandle entity) {
    IkChain* chain = ctx.world.get<IkChain>(entity);
    if (!chain) return;
    captureComponentBefore(ctx, entity, kChainName);
    bool committed = false;
    auto track = [&] { committed |= ImGui::IsItemDeactivatedAfterEdit(); };
    ImGui::Checkbox("Enabled", &chain->enabled);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Weight", &chain->weight, 0.0f, 1.0f);
    track();
    int algorithm = static_cast<int>(chain->algorithm);
    if (ImGui::Combo("Algorithm", &algorithm, "Auto\0Two-bone\0FABRIK\0CCD\0")) {
        chain->algorithm = static_cast<IkAlgorithm>(algorithm);
        committed = true;
    }
    ImGui::SliderInt("Iterations", &chain->iterations, 1, kMaxIterations);
    track();
    ImGui::Checkbox("Target is a point", &chain->useTargetPoint);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (chain->useTargetPoint) {
        ImGui::DragFloat3("Target point", &chain->targetPoint.x, 0.05f);
        track();
    } else {
        committed |= drawEntityPicker(ctx, "Target", chain->target);
    }
    ImGui::Checkbox("Pole is a point", &chain->usePolePoint);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (chain->usePolePoint) {
        ImGui::DragFloat3("Pole point", &chain->polePoint.x, 0.05f);
        track();
    } else {
        committed |= drawEntityPicker(ctx, "Pole", chain->pole);
    }
    ImGui::TextDisabled("%zu joints  |  %s  |  error %.4f", chain->joints.size(), chain->lastReached ? "reached" : "out of reach", chain->lastError);
    drawDebugToggle(ctx);
    if (committed) commitComponentEdit(ctx, entity, kChainName);
}

void Inspector::drawWalkerComponent(EditorContext& ctx, EntityHandle entity) {
    Walker* walker = ctx.world.get<Walker>(entity);
    if (!walker) return;
    captureComponentBefore(ctx, entity, kWalkerName);
    bool committed = false;
    auto track = [&] { committed |= ImGui::IsItemDeactivatedAfterEdit(); };
    ImGui::Checkbox("Enabled", &walker->enabled);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SeparatorText("Gait");
    ImGui::DragFloat("Stride", &walker->stride, 0.01f, 0.01f, kMaxLength, "%.2f m");
    track();
    ImGui::DragFloat("Step height", &walker->stepHeight, 0.01f, 0.0f, kMaxLength, "%.2f m");
    track();
    ImGui::DragFloat("Swing speed", &walker->swingSpeed, 0.05f, 0.1f, kMaxSpeed);
    track();
    ImGui::DragFloat("Min swing time", &walker->minSwingTime, 0.005f, 0.01f, kMaxSwingTime, "%.3f s");
    track();
    ImGui::DragFloat("Max swing time", &walker->maxSwingTime, 0.005f, 0.01f, kMaxSwingTime, "%.3f s");
    track();
    ImGui::SliderInt("Max simultaneous swings", &walker->maxSimultaneous, 0, static_cast<int>(std::max<size_t>(walker->legs.size(), 1)));
    track();
    ImGui::SeparatorText("Body");
    ImGui::DragFloat("Max speed", &walker->maxSpeed, 0.05f, 0.0f, kMaxSpeed, "%.2f m/s");
    track();
    ImGui::DragFloat("Acceleration", &walker->acceleration, 0.1f, 0.1f, kMaxSpeed * 4.0f);
    track();
    ImGui::DragFloat("Turn rate", &walker->turnRate, 0.05f, 0.0f, kMaxSpeed);
    track();
    ImGui::DragFloat("Body height", &walker->bodyHeight, 0.01f, 0.0f, kMaxLength, "%.2f m");
    track();
    ImGui::SliderFloat("Max tilt", &walker->maxTiltDegrees, 0.0f, kMaxTiltDegrees, "%.0f deg");
    track();
    ImGui::SeparatorText("Steering");
    ImGui::Checkbox("Has move target", &walker->hasMoveTarget);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (walker->hasMoveTarget) {
        ImGui::DragFloat3("Move target", &walker->moveTarget.x, 0.05f);
        track();
    }
    if (ImGui::Button("Target selection") && !ctx.primary().isNull() && ctx.primary() != entity) {
        setWalkerTarget(ctx.world, entity, ctx.world.getWorldPosition(ctx.primary()));
        committed = true;
    }
    ImGui::TextDisabled("%zu legs  |  speed %.2f m/s  |  %s", walker->legs.size(), walker->speed, walker->arrived ? "arrived" : "walking");
    drawDebugToggle(ctx);
    if (committed) commitComponentEdit(ctx, entity, kWalkerName);
}
