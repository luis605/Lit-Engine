module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.History;
import Engine.Render.material;
import Engine.glm;

namespace {

constexpr float PRESET_BUTTON_SPACING = 4.0f;

struct Preset {
    const char* name;
    std::function<void(Material&)> apply;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"Plastic", [](Material& material) { material.metallic = 0.0f; material.roughness = 0.35f; material.reflectance = 0.5f; material.clearcoat = 0.0f; }},
        {"Rubber", [](Material& material) { material.metallic = 0.0f; material.roughness = 0.9f; material.reflectance = 0.35f; }},
        {"Steel", [](Material& material) { material.baseColor = glm::vec3(0.56f, 0.57f, 0.58f); material.metallic = 1.0f; material.roughness = 0.3f; }},
        {"Gold", [](Material& material) { material.baseColor = glm::vec3(1.0f, 0.766f, 0.336f); material.metallic = 1.0f; material.roughness = 0.2f; }},
        {"Copper", [](Material& material) { material.baseColor = glm::vec3(0.955f, 0.638f, 0.538f); material.metallic = 1.0f; material.roughness = 0.25f; }},
        {"Brushed", [](Material& material) { material.baseColor = glm::vec3(0.91f, 0.92f, 0.92f); material.metallic = 1.0f; material.roughness = 0.35f; material.anisotropy = 0.8f; }},
        {"Car paint", [](Material& material) { material.metallic = 0.4f; material.roughness = 0.4f; material.clearcoat = 1.0f; material.clearcoatRoughness = 0.03f; }},
        {"Velvet", [](Material& material) { material.metallic = 0.0f; material.roughness = 0.8f; material.sheenColor = glm::vec3(1.0f); material.sheenRoughness = 0.35f; }},
        {"Skin", [](Material& material) { material.baseColor = glm::vec3(0.85f, 0.62f, 0.52f); material.metallic = 0.0f; material.roughness = 0.55f; material.subsurface = 0.6f; material.subsurfaceColor = glm::vec3(0.9f, 0.3f, 0.2f); }},
        {"Glass", [](Material& material) { material.metallic = 0.0f; material.roughness = 0.02f; material.transmission = 1.0f; material.ior = 1.5f; material.opacity = 0.25f; }},
        {"Emissive", [](Material& material) { material.emissive = material.baseColor; material.emissiveIntensity = 8.0f; }},
    };
    return list;
}

struct EditTracker {
    bool changed = false;
    bool activated = false;
    bool committed = false;

    void track() {
        activated |= ImGui::IsItemActivated();
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    void slider(const char* label, float& value, float minimum, float maximum) {
        changed |= ImGui::SliderFloat(label, &value, minimum, maximum);
        track();
    }
    void color(const char* label, glm::vec3& value) {
        changed |= ImGui::ColorEdit3(label, &value.x, ImGuiColorEditFlags_Float);
        track();
    }
};

void recordMaterialChange(EditorContext& ctx, const char* label, uint32_t id, const Material& before, const Material& after) {
    Engine& engine = ctx.engine;
    ctx.history->record(label, [&engine, id, after] { engine.setMaterial(id, after); }, [&engine, id, before] { engine.setMaterial(id, before); });
}

void drawPresets(EditorContext& ctx, uint32_t id, Material& material) {
    ImGui::TextDisabled("Presets");
    float cursorX = ImGui::GetCursorPosX();
    const float right = ImGui::GetContentRegionAvail().x + cursorX;
    const std::vector<Preset>& list = presets();
    for (const Preset& preset : list) {
        const float width = ImGui::CalcTextSize(preset.name).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (cursorX + width > right) {
            cursorX = ImGui::GetStyle().WindowPadding.x;
        } else if (&preset != &list.front()) {
            ImGui::SameLine(0.0f, PRESET_BUTTON_SPACING);
        }
        if (ImGui::SmallButton(preset.name)) {
            const Material before = material;
            preset.apply(material);
            ctx.engine.setMaterial(id, material);
            recordMaterialChange(ctx, "Material preset", id, before, material);
        }
        cursorX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + PRESET_BUTTON_SPACING;
    }
}

void drawSurface(EditTracker& tracker, Material& material) {
    ImGui::SeparatorText("Surface");
    tracker.color("Base color", material.baseColor);
    tracker.slider("Metallic", material.metallic, 0.0f, 1.0f);
    tracker.slider("Roughness", material.roughness, 0.0f, 1.0f);
    tracker.slider("Reflectance", material.reflectance, 0.0f, 1.0f);
    tracker.slider("Opacity", material.opacity, 0.0f, 1.0f);
}

void drawLayeredEffects(EditTracker& tracker, Material& material) {
    if (ImGui::TreeNode("Emission")) {
        tracker.color("Color", material.emissive);
        tracker.changed |= ImGui::DragFloat("Intensity", &material.emissiveIntensity, 0.05f, 0.0f, 10000.0f, "%.2f");
        tracker.track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Clear coat")) {
        tracker.slider("Amount", material.clearcoat, 0.0f, 1.0f);
        tracker.slider("Roughness##cc", material.clearcoatRoughness, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Sheen")) {
        tracker.color("Color##sheen", material.sheenColor);
        tracker.slider("Roughness##sheen", material.sheenRoughness, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Anisotropy")) {
        tracker.slider("Strength", material.anisotropy, -1.0f, 1.0f);
        tracker.slider("Rotation", material.anisotropyRotation, 0.0f, 1.0f);
        ImGui::TreePop();
    }
}

void drawVolumeEffects(EditTracker& tracker, Material& material) {
    if (ImGui::TreeNode("Subsurface")) {
        tracker.slider("Amount##ss", material.subsurface, 0.0f, 1.0f);
        tracker.color("Scatter", material.subsurfaceColor);
        tracker.slider("Thickness", material.thickness, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Transmission")) {
        tracker.slider("Amount##tr", material.transmission, 0.0f, 1.0f);
        tracker.slider("IOR", material.ior, 1.0f, 2.5f);
        ImGui::TreePop();
    }
}

void drawMapping(EditTracker& tracker, Material& material) {
    if (!ImGui::TreeNode("Mapping")) return;
    tracker.changed |= ImGui::DragFloat("UV scale", &material.uvScale, 0.01f, 0.01f, 1000.0f);
    tracker.track();
    tracker.changed |= ImGui::Checkbox("Triplanar", &material.triplanar);
    tracker.track();
    tracker.slider("Normal strength", material.normalStrength, 0.0f, 2.0f);
    tracker.slider("Occlusion", material.occlusion, 0.0f, 1.0f);
    tracker.changed |= ImGui::DragFloat4("Custom 0", &material.custom0.x, 0.01f);
    tracker.track();
    tracker.changed |= ImGui::DragFloat4("Custom 1", &material.custom1.x, 0.01f);
    tracker.track();
    ImGui::TreePop();
}

}

void Inspector::drawMaterial(EditorContext& ctx, uint32_t id) {
    Engine& engine = ctx.engine;
    Material material = engine.material(id);
    static Material beforeEdit;
    EditTracker tracker;
    ImGui::PushID(static_cast<int>(id));
    ImGui::SetNextItemWidth(-1.0f);
    tracker.changed |= ImGui::InputText("##materialName", &material.name);
    tracker.track();

    drawPresets(ctx, id, material);
    drawSurface(tracker, material);
    drawLayeredEffects(tracker, material);
    drawVolumeEffects(tracker, material);
    drawMapping(tracker, material);

    if (tracker.activated) beforeEdit = engine.material(id);
    if (tracker.changed) engine.setMaterial(id, material);
    if (tracker.committed) recordMaterialChange(ctx, "Edit material", id, beforeEdit, engine.material(id));
    ImGui::PopID();
}
