module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <string>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.glm;
import Engine.Render.reflection;

namespace {

constexpr float kMaxVolumeExtent = 10000.0f;
constexpr float kMaxProbeIntensity = 8.0f;
constexpr float kExtentDragSpeed = 0.1f;
constexpr int kMaxPickerRows = 200;
constexpr const char* kProbeName = "ReflectionProbe";
constexpr const char* kPlanarName = "PlanarReflection";

}

void Inspector::commitComponentEdit(EditorContext& ctx, EntityHandle entity, const std::string& name) {
    World& world = ctx.world;
    const std::string before = m_componentBefore;
    const std::string after = world.componentText(entity, name).value_or("");
    m_componentBefore = after;
    if (before == after) return;
    ctx.history->record(
        "Edit " + name, [&world, entity, name, after] { world.setComponentFromText(entity, name, after); },
        [&world, entity, name, before] { world.setComponentFromText(entity, name, before); });
}

void Inspector::captureComponentBefore(EditorContext& ctx, EntityHandle entity, const std::string& name) {
    if (!ImGui::IsAnyItemActive()) m_componentBefore = ctx.world.componentText(entity, name).value_or("");
}

bool Inspector::drawEntityPicker(EditorContext& ctx, const char* label, EntityHandle& value) {
    World& world = ctx.world;
    const std::string preview = value.isNull() || !world.isAlive(value) ? "none" : std::format("{}  #{}", world.getName(value), value.index);
    bool changed = false;
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##pickerFilter", "filter", &m_pickerFilter);
        if (ImGui::Selectable("none", value.isNull())) {
            value = NULL_ENTITY;
            changed = true;
        }
        int rows = 0;
        world.forEach([&](EntityHandle candidate) {
            if (rows >= kMaxPickerRows) return;
            const std::string text = std::format("{}  #{}", world.getName(candidate), candidate.index);
            if (!m_pickerFilter.empty() && fuzzyScore(m_pickerFilter, text) <= 0.0f) return;
            ++rows;
            if (ImGui::Selectable(text.c_str(), candidate == value)) {
                value = candidate;
                changed = true;
            }
        });
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("Use sel.") && !ctx.primary().isNull()) {
        value = ctx.primary();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

void Inspector::drawProbeComponent(EditorContext& ctx, EntityHandle entity) {
    ReflectionProbeComponent* probe = ctx.world.get<ReflectionProbeComponent>(entity);
    if (!probe) return;
    captureComponentBefore(ctx, entity, kProbeName);
    bool committed = false;
    auto track = [&] { committed |= ImGui::IsItemDeactivatedAfterEdit(); };
    ImGui::DragFloat3("Half extents", &probe->halfExtents.x, kExtentDragSpeed, 0.1f, kMaxVolumeExtent, "%.2f m");
    track();
    ImGui::SliderFloat("Intensity", &probe->intensity, 0.0f, kMaxProbeIntensity);
    track();
    ImGui::DragFloat("Blend distance", &probe->blendDistance, kExtentDragSpeed, 0.0f, kMaxVolumeExtent, "%.2f m");
    track();
    int resolutionIndex = 0;
    while ((kProbeResolutionMin << resolutionIndex) < clampProbeResolution(probe->resolution)) ++resolutionIndex;
    const char* resolutionLabels[] = {"32", "64", "128", "256"};
    if (ImGui::Combo("Resolution", &resolutionIndex, resolutionLabels, IM_ARRAYSIZE(resolutionLabels))) {
        probe->resolution = kProbeResolutionMin << resolutionIndex;
        committed = true;
    }
    if (ImGui::Button("Bake now")) ctx.engine.bakeReflectionProbes();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Probes bake once when added or resized; press to rebake after the scene or sky changes");
    ImGui::SameLine();
    if (ImGui::Button("Fit to children")) {
        glm::vec3 low(1.0e9f);
        glm::vec3 high(-1.0e9f);
        const glm::vec3 center = ctx.world.getWorldPosition(entity);
        for (EntityHandle child : ctx.world.getChildren(entity)) {
            low = glm::min(low, ctx.world.getWorldPosition(child));
            high = glm::max(high, ctx.world.getWorldPosition(child));
        }
        if (low.x <= high.x) {
            probe->halfExtents = glm::max(glm::max(glm::abs(low - center), glm::abs(high - center)), glm::vec3(0.5f));
            committed = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grow the box to enclose this entity's children");
    ImGui::SameLine();
    ImGui::TextDisabled("box shown in the viewport (cyan)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Palette: view.reflectionVolumes toggles the volume overlay. Needs reflection mode SSR + Probes or higher.");
    if (committed) commitComponentEdit(ctx, entity, kProbeName);
}

void Inspector::drawPlanarComponent(EditorContext& ctx, EntityHandle entity) {
    PlanarReflectionComponent* plane = ctx.world.get<PlanarReflectionComponent>(entity);
    if (!plane) return;
    captureComponentBefore(ctx, entity, kPlanarName);
    bool committed = false;
    ImGui::DragFloat2("Half size", &plane->halfSize.x, kExtentDragSpeed, 0.1f, kMaxVolumeExtent, "%.2f m");
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Plane is the entity's local +Y; needs reflection mode SSR + Planar");
    if (committed) commitComponentEdit(ctx, entity, kPlanarName);
}
