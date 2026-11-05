module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.material;
import Engine.glm;

namespace {

constexpr float COMBO_RESERVED_WIDTH = -60.0f;
constexpr float REMOVE_BUTTON_RESERVED_WIDTH = 22.0f;
constexpr float MAX_SPOT_CONE_DEGREES = 89.0f;
constexpr float kMaxLightIntensity = 1.0e9f;

using ComponentList = std::vector<std::pair<std::string, std::string>>;

}

void Inspector::assignRender(EditorContext& ctx, const char* label, bool changeMesh, uint32_t mesh, bool changeMaterial, uint32_t material, bool changeShader, uint32_t shader, bool changeAlpha, float alpha) {
    World& world = ctx.world;
    ctx.history->beginGroup(label);
    for (EntityHandle selected : ctx.topLevelSelection()) {
        const RenderableComponent current = world.getRenderable(selected);
        ctx.history->setRender(selected, changeMesh ? mesh : world.getMesh(selected), changeMaterial ? material : current.material_uuid, changeShader ? shader : current.shaderId, changeAlpha ? alpha : current.alpha);
    }
    ctx.history->endGroup();
}

void Inspector::drawMeshCombo(EditorContext& ctx, EntityHandle entity) {
    Engine& engine = ctx.engine;
    const uint32_t mesh = ctx.world.getMesh(entity);
    const std::string currentLabel = meshLabel(engine, mesh);
    ImGui::SetNextItemWidth(COMBO_RESERVED_WIDTH);
    if (!ImGui::BeginCombo("Mesh", currentLabel.c_str())) return;
    if (ImGui::Selectable("empty", mesh == NO_MESH)) assignRender(ctx, "Set mesh", true, NO_MESH, false, 0, false, 0, false, 1.0f);
    for (const std::string& name : engine.availableMeshes()) {
        if (ImGui::Selectable(name.c_str(), currentLabel == name)) assignRender(ctx, "Set mesh", true, engine.loadMesh(name), false, 0, false, 0, false, 1.0f);
    }
    ImGui::EndCombo();
}

void Inspector::drawMaterialCombo(EditorContext& ctx, uint32_t material) {
    Engine& engine = ctx.engine;
    ImGui::SetNextItemWidth(COMBO_RESERVED_WIDTH);
    if (!ImGui::BeginCombo("Material", std::format("{}  #{}", engine.material(material).name, material).c_str(), ImGuiComboFlags_HeightLarge)) return;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##mf", "filter", &m_materialFilter);
    if (ImGui::Selectable("+ New material")) {
        Material fresh = engine.material(material);
        fresh.name = std::format("Material {}", engine.materialCount());
        const uint32_t newId = engine.createMaterial(fresh);
        if (newId != 0) assignRender(ctx, "New material", false, 0, true, newId, false, 0, false, 1.0f);
    }
    for (uint32_t id = 0; id < engine.materialCount(); ++id) {
        const std::string label = std::format("{}  #{}", engine.material(id).name, id);
        if (!m_materialFilter.empty() && fuzzyScore(m_materialFilter, label) <= 0.0f) continue;
        if (ImGui::Selectable(label.c_str(), id == material)) assignRender(ctx, "Assign material", false, 0, true, id, false, 0, false, 1.0f);
    }
    ImGui::EndCombo();
}

void Inspector::drawShaderCombo(EditorContext& ctx, uint32_t shaderId) {
    const std::vector<std::string> shaders = ctx.engine.shaderNames();
    const std::string preview = shaderId < shaders.size() ? shaders[shaderId] : std::format("shader {}", shaderId);
    ImGui::SetNextItemWidth(COMBO_RESERVED_WIDTH);
    if (!ImGui::BeginCombo("Shader", preview.c_str())) return;
    for (uint32_t id = 0; id < shaders.size(); ++id) {
        if (ImGui::Selectable(shaders[id].c_str(), id == shaderId)) assignRender(ctx, "Set shader", false, 0, false, 0, true, id, false, 1.0f);
    }
    ImGui::EndCombo();
}

void Inspector::drawAlphaSlider(EditorContext& ctx, float currentAlpha) {
    static float alphaBefore = 1.0f;
    float alpha = currentAlpha;
    ImGui::SetNextItemWidth(COMBO_RESERVED_WIDTH);
    if (ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f)) {
        for (EntityHandle selected : ctx.topLevelSelection()) ctx.world.setAlpha(selected, alpha);
    }
    if (ImGui::IsItemActivated()) alphaBefore = currentAlpha;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        for (EntityHandle selected : ctx.topLevelSelection()) ctx.world.setAlpha(selected, alphaBefore);
        assignRender(ctx, "Alpha", false, 0, false, 0, false, 0, true, alpha);
    }
}

void Inspector::drawRender(EditorContext& ctx, EntityHandle entity) {
    if (!ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const RenderableComponent renderable = ctx.world.getRenderable(entity);
    const uint32_t material = renderable.material_uuid;
    drawMeshCombo(ctx, entity);
    drawMaterialCombo(ctx, material);
    drawShaderCombo(ctx, renderable.shaderId);
    drawAlphaSlider(ctx, renderable.alpha);
    if (ImGui::TreeNodeEx("Material##editor", 0, "Edit material  ·  %s", ctx.engine.material(material).name.c_str())) {
        if (material == 0) ImGui::TextDisabled("Default material: shared by every object without one");
        drawMaterial(ctx, material);
        ImGui::TreePop();
    }
}

void Inspector::drawLightComponent(EditorContext& ctx, EntityHandle entity) {
    World& world = ctx.world;
    LightComponent* light = world.get<LightComponent>(entity);
    if (!light) return;
    static LightComponent before;
    if (!ImGui::IsAnyItemActive()) before = *light;
    bool committed = false;
    auto track = [&] { committed |= ImGui::IsItemDeactivatedAfterEdit(); };
    int type = static_cast<int>(light->type);
    if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
        light->type = static_cast<LightComponent::Type>(type);
        committed = true;
    }
    ImGui::ColorEdit3("Color", &light->color.x, ImGuiColorEditFlags_Float);
    track();
    ImGui::DragFloat("Intensity", &light->intensity, std::max(0.05f, light->intensity * 0.01f), 0.0f, kMaxLightIntensity, light->type == LightComponent::Type::Directional ? "%.0f lux" : "%.0f lm");
    track();
    drawIntensityPresets(ctx, *light, committed);
    if (light->type != LightComponent::Type::Directional) {
        ImGui::DragFloat("Range", &light->range, 0.1f, 0.01f, 100000.0f);
        track();
    }
    if (light->type == LightComponent::Type::Spot) {
        ImGui::SliderFloat("Inner cone", &light->innerConeDegrees, 0.0f, MAX_SPOT_CONE_DEGREES);
        track();
        ImGui::SliderFloat("Outer cone", &light->outerConeDegrees, 0.0f, MAX_SPOT_CONE_DEGREES);
        track();
    }
    if (light->type != LightComponent::Type::Directional) {
        ImGui::DragFloat("Source radius", &light->sourceRadius, 0.01f, 0.0f, 50.0f);
        track();
        ImGui::DragFloat("Tube length", &light->sourceLength, 0.01f, 0.0f, 100.0f);
        track();
    }
    ImGui::SliderFloat("Specular", &light->specular, 0.0f, 1.0f);
    track();
    if (!committed) return;
    const LightComponent old = before;
    const LightComponent now = *light;
    before = now;
    ctx.history->record(
        "Edit light", [&world, entity, now] { if (LightComponent* target = world.get<LightComponent>(entity)) *target = now; },
        [&world, entity, old] { if (LightComponent* target = world.get<LightComponent>(entity)) *target = old; });
}

void Inspector::drawCameraComponent(EditorContext& ctx, EntityHandle entity) {
    World& world = ctx.world;
    CameraComponent* camera = world.get<CameraComponent>(entity);
    if (!camera) return;
    ImGui::SliderFloat("FOV", &camera->fov, 10.0f, 150.0f);
    ImGui::DragFloat("Near", &camera->nearPlane, 0.01f, 0.001f, 100.0f);
    ImGui::DragFloat("Far", &camera->farPlane, 1.0f, 1.0f, 1000000.0f);
    const bool active = world.getActiveCamera() == entity;
    if (ImGui::Button(active ? "Active camera" : "Make active")) world.setActiveCamera(active ? NULL_ENTITY : entity);
}

void Inspector::drawTextComponent(EditorContext& ctx, EntityHandle entity, const std::string& name, const std::string& text) {
    const std::string key = name + "#" + std::to_string(entity.index);
    if (m_componentEditing != key) m_componentBuffer = text;
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = ImGui::InputText("##text", &m_componentBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive()) m_componentEditing = key;
    if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
        if (!ctx.history->setComponentText(entity, name, m_componentBuffer)) ctx.toast(std::format("'{}' rejected that value", name), ToastKind::Error);
        m_componentEditing.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Serialized text; Enter applies (undoable)");
}

void Inspector::drawAddComponent(EditorContext& ctx, EntityHandle entity, const ComponentList& existing) {
    World& world = ctx.world;
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##add", "+ Add component")) return;
    for (const std::string& name : world.componentNames()) {
        const bool present = std::any_of(existing.begin(), existing.end(), [&](const auto& component) { return component.first == name; });
        if (present || !ImGui::Selectable(name.c_str())) continue;
        if (!world.addComponentByName(entity, name)) continue;
        const std::string after = world.componentText(entity, name).value_or("");
        ctx.history->record(
            "Add component", [&world, entity, name, after] { world.setComponentFromText(entity, name, after); },
            [&world, entity, name] { world.removeComponentByName(entity, name); });
    }
    ImGui::EndCombo();
}

void Inspector::drawComponents(EditorContext& ctx, EntityHandle entity, bool expand) {
    World& world = ctx.world;
    const EntityDescription description = world.describeEntity(entity);
    const std::string header = std::format("Components ({})###components{}", description.components.size(), expand ? "Open" : "");
    if (!ImGui::CollapsingHeader(header.c_str(), expand ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    for (const auto& [name, text] : description.components) {
        ImGui::PushID(name.c_str());
        const bool open = ImGui::TreeNodeEx("##component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap, "%s", name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - REMOVE_BUTTON_RESERVED_WIDTH);
        if (ImGui::SmallButton("x")) {
            const std::string componentName = name;
            const std::string componentText = text;
            world.removeComponentByName(entity, componentName);
            ctx.history->record(
                "Remove component", [&world, entity, componentName] { world.removeComponentByName(entity, componentName); },
                [&world, entity, componentName, componentText] { world.setComponentFromText(entity, componentName, componentText); });
            if (open) ImGui::TreePop();
            ImGui::PopID();
            break;
        }
        if (open) {
            if (name == "Light") {
                drawLightComponent(ctx, entity);
            } else if (name == "Camera") {
                drawCameraComponent(ctx, entity);
            } else if (name == "ReflectionProbe") {
                drawProbeComponent(ctx, entity);
            } else if (name == "PlanarReflection") {
                drawPlanarComponent(ctx, entity);
            } else if (name == "IkChain") {
                drawIkChainComponent(ctx, entity);
            } else if (name == "Walker") {
                drawWalkerComponent(ctx, entity);
            } else {
                drawTextComponent(ctx, entity, name, text);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    for (const auto& script : description.scripts) ImGui::BulletText("script  %s", script.first.c_str());
    drawAddComponent(ctx, entity, description.components);
}
