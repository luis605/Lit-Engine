module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <string>
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

glm::quat eulerToQuat(const glm::vec3& degrees) {
    const glm::vec3 r = glm::radians(degrees);
    return glm::angleAxis(r.z, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(r.y, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(r.x, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 quatToEuler(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(glm::normalize(q));
    const float sy = std::clamp(-m[0][2], -1.0f, 1.0f);
    const float y = std::asin(sy);
    float x = 0.0f;
    float z = 0.0f;
    if (std::abs(sy) < 0.9999f) {
        x = std::atan2(m[1][2], m[2][2]);
        z = std::atan2(m[0][1], m[0][0]);
    } else {
        x = std::atan2(-m[2][1], m[1][1]);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

glm::mat4 compose(const glm::vec3& p, const glm::quat& r, const glm::vec3& s) { return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s); }

bool axisDrag(const char* label, glm::vec3& value, float speed, const char* format, bool& activated, bool& deactivated) {
    ImGui::PushID(label);
    bool changed = false;
    activated = false;
    deactivated = false;
    const float full = ImGui::GetContentRegionAvail().x;
    const float labelWidth = 64.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    const float each = (full - labelWidth - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f) / 3.0f;
    const ImU32 colors[3] = {IM_COL32(230, 80, 80, 255), IM_COL32(110, 210, 90, 255), IM_COL32(80, 140, 240, 255)};
    const char* ids[3] = {"##x", "##y", "##z"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(each);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        changed |= ImGui::DragFloat(ids[i], &value[i], speed, 0.0f, 0.0f, format);
        activated |= ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + ImGui::GetFrameHeight()), colors[i], 3.0f, ImDrawFlags_RoundCornersLeft);
    }
    ImGui::PopID();
    return changed;
}

struct Preset {
    const char* name;
    std::function<void(Material&)> apply;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"Plastic", [](Material& m) { m.metallic = 0.0f; m.roughness = 0.35f; m.reflectance = 0.5f; m.clearcoat = 0.0f; }},
        {"Rubber", [](Material& m) { m.metallic = 0.0f; m.roughness = 0.9f; m.reflectance = 0.35f; }},
        {"Steel", [](Material& m) { m.baseColor = glm::vec3(0.56f, 0.57f, 0.58f); m.metallic = 1.0f; m.roughness = 0.3f; }},
        {"Gold", [](Material& m) { m.baseColor = glm::vec3(1.0f, 0.766f, 0.336f); m.metallic = 1.0f; m.roughness = 0.2f; }},
        {"Copper", [](Material& m) { m.baseColor = glm::vec3(0.955f, 0.638f, 0.538f); m.metallic = 1.0f; m.roughness = 0.25f; }},
        {"Brushed", [](Material& m) { m.baseColor = glm::vec3(0.91f, 0.92f, 0.92f); m.metallic = 1.0f; m.roughness = 0.35f; m.anisotropy = 0.8f; }},
        {"Car paint", [](Material& m) { m.metallic = 0.4f; m.roughness = 0.4f; m.clearcoat = 1.0f; m.clearcoatRoughness = 0.03f; }},
        {"Velvet", [](Material& m) { m.metallic = 0.0f; m.roughness = 0.8f; m.sheenColor = glm::vec3(1.0f); m.sheenRoughness = 0.35f; }},
        {"Skin", [](Material& m) { m.baseColor = glm::vec3(0.85f, 0.62f, 0.52f); m.metallic = 0.0f; m.roughness = 0.55f; m.subsurface = 0.6f; m.subsurfaceColor = glm::vec3(0.9f, 0.3f, 0.2f); }},
        {"Glass", [](Material& m) { m.metallic = 0.0f; m.roughness = 0.02f; m.transmission = 1.0f; m.ior = 1.5f; m.opacity = 0.25f; }},
        {"Emissive", [](Material& m) { m.emissive = m.baseColor; m.emissiveIntensity = 8.0f; }},
    };
    return list;
}

}  // namespace

void Inspector::beginTransformEdit(EditorContext& ctx) {
    if (m_editing) return;
    m_editing = true;
    m_editStart.clear();
    for (EntityHandle e : ctx.topLevelSelection()) m_editStart.push_back({e, ctx.world.getLocalMatrix(e)});
}

void Inspector::endTransformEdit(EditorContext& ctx, const char* label) {
    if (!m_editing) return;
    m_editing = false;
    ctx.history->beginGroup(label);
    for (const Snapshot& s : m_editStart) {
        if (ctx.world.isAlive(s.entity)) ctx.history->commitLocalMatrix(s.entity, s.local, ctx.world.getLocalMatrix(s.entity));
    }
    ctx.history->endGroup();
    m_editStart.clear();
}

void Inspector::drawHeader(EditorContext& ctx, EntityHandle e) {
    World& world = ctx.world;
    if (m_bufferEntity != e || !ImGui::IsAnyItemActive()) {
        m_nameBuffer = world.getName(e);
        m_tagBuffer = world.getTag(e);
        m_bufferEntity = e;
    }
    if (m_focusName) {
        ImGui::SetKeyboardFocusHere();
        m_focusName = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::InputTextWithHint("##name", displayName(world, e).c_str(), &m_nameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) ctx.history->setName(e, m_nameBuffer);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.history->setName(e, m_nameBuffer);
    ImGui::SameLine();
    ImGui::TextDisabled("#%u", e.index);

    bool visible = world.isVisible(e);
    if (ImGui::Checkbox("Visible", &visible)) {
        ctx.history->beginGroup("Visibility");
        for (EntityHandle s : ctx.topLevelSelection()) ctx.history->setVisible(s, visible);
        ctx.history->endGroup();
    }
    ImGui::SameLine();
    bool occluder = (world.getRenderFlags(e) & RENDER_NO_OCCLUDER) == 0;
    if (ImGui::Checkbox("Occluder", &occluder)) {
        std::vector<EntityHandle> targets = ctx.topLevelSelection();
        auto set = [&world, targets](bool on) {
            for (EntityHandle t : targets) world.setRenderFlags(t, RENDER_NO_OCCLUDER, !on);
        };
        set(occluder);
        ctx.history->record("Occluder", [set, occluder] { set(occluder); }, [set, occluder] { set(!occluder); });
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: drawn after the occlusion buffer so it never hides other objects");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##tag", "tag", &m_tagBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) ctx.history->setTag(e, m_tagBuffer);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.history->setTag(e, m_tagBuffer);

    const uint32_t layer = world.getLayer(e);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Layers");
    for (int bit = 0; bit < 8; ++bit) {
        ImGui::SameLine(0.0f, 3.0f);
        const bool on = (layer & (1u << bit)) != 0;
        ImGui::PushID(bit);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(1.0f, 0.69f, 0.26f, 0.55f) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        if (ImGui::SmallButton(std::format("{}", bit).c_str())) {
            const uint32_t next = layer ^ (1u << bit);
            std::vector<EntityHandle> targets = ctx.topLevelSelection();
            std::vector<uint32_t> before;
            for (EntityHandle t : targets) before.push_back(world.getLayer(t));
            for (EntityHandle t : targets) world.setLayer(t, next);
            ctx.history->record("Layer", [&world, targets, next] { for (EntityHandle t : targets) world.setLayer(t, next); }, [&world, targets, before] {
                for (size_t i = 0; i < targets.size(); ++i) world.setLayer(targets[i], before[i]);
            });
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    const EntityHandle parent = world.getParent(e);
    if (!parent.isNull()) {
        ImGui::TextDisabled("Parent");
        ImGui::SameLine();
        if (ImGui::SmallButton(displayName(world, parent).c_str())) ctx.selection.set(parent);
        ImGui::SameLine();
        if (ImGui::SmallButton("Unparent")) ctx.history->setParent(e, NULL_ENTITY);
    }
}

void Inspector::drawTransform(EditorContext& ctx, EntityHandle e) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;
    World& world = ctx.world;
    glm::vec3 position = world.getPosition(e);
    glm::vec3 scale = world.getScale(e);
    const glm::quat rotationQ = world.getRotation(e);
    glm::vec3 euler = quatToEuler(rotationQ);
    bool activated = false, deactivated = false;
    const glm::vec3 oldPosition = position;
    if (axisDrag("Position", position, 0.05f, "%.3f", activated, deactivated)) {
        const glm::vec3 delta = position - oldPosition;
        for (EntityHandle s : ctx.topLevelSelection()) world.setPosition(s, s == e ? position : world.getPosition(s) + delta);
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Move");

    const glm::vec3 oldEuler = euler;
    if (axisDrag("Rotation", euler, 0.5f, "%.1f°", activated, deactivated)) {
        const glm::quat next = eulerToQuat(euler);
        const glm::quat delta = next * glm::inverse(eulerToQuat(oldEuler));
        for (EntityHandle s : ctx.topLevelSelection()) world.setRotation(s, s == e ? next : glm::normalize(delta * world.getRotation(s)));
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Rotate");

    const glm::vec3 oldScale = scale;
    if (axisDrag("Scale", scale, 0.01f, "%.3f", activated, deactivated)) {
        glm::vec3 ratio(1.0f);
        for (int i = 0; i < 3; ++i) ratio[i] = std::abs(oldScale[i]) > 1e-6f ? scale[i] / oldScale[i] : 1.0f;
        for (EntityHandle s : ctx.topLevelSelection()) world.setScale(s, s == e ? scale : world.getScale(s) * ratio);
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Scale");

    const glm::vec3 worldPosition = world.getWorldPosition(e);
    ImGui::TextDisabled("World  %.2f  %.2f  %.2f", worldPosition.x, worldPosition.y, worldPosition.z);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110.0f + ImGui::GetCursorPosX() - ImGui::GetCursorPosX());
    if (ImGui::SmallButton("Reset")) {
        ctx.history->beginGroup("Reset transform");
        for (EntityHandle s : ctx.topLevelSelection()) ctx.history->setLocalMatrix(s, compose(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)));
        ctx.history->endGroup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(std::format("{} {} {}", position.x, position.y, position.z).c_str());
}

void Inspector::drawMaterial(EditorContext& ctx, uint32_t id) {
    Engine& engine = ctx.engine;
    Material m = engine.material(id);
    static Material before;
    bool changed = false;
    bool activated = false;
    bool committed = false;
    auto track = [&] {
        activated |= ImGui::IsItemActivated();
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::PushID(static_cast<int>(id));
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::InputText("##materialName", &m.name);
    track();

    ImGui::TextDisabled("Presets");
    float x = ImGui::GetCursorPosX();
    const float right = ImGui::GetContentRegionAvail().x + x;
    for (const Preset& preset : presets()) {
        const float w = ImGui::CalcTextSize(preset.name).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (x + w > right) {
            x = ImGui::GetStyle().WindowPadding.x;
        } else if (&preset != &presets().front()) {
            ImGui::SameLine(0.0f, 4.0f);
        }
        if (ImGui::SmallButton(preset.name)) {
            const Material old = m;
            preset.apply(m);
            engine.setMaterial(id, m);
            const Material now = m;
            ctx.history->record("Material preset", [&engine, id, now] { engine.setMaterial(id, now); }, [&engine, id, old] { engine.setMaterial(id, old); });
        }
        x = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 4.0f;
    }

    ImGui::SeparatorText("Surface");
    changed |= ImGui::ColorEdit3("Base color", &m.baseColor.x, ImGuiColorEditFlags_Float);
    track();
    changed |= ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
    track();
    changed |= ImGui::SliderFloat("Roughness", &m.roughness, 0.0f, 1.0f);
    track();
    changed |= ImGui::SliderFloat("Reflectance", &m.reflectance, 0.0f, 1.0f);
    track();
    changed |= ImGui::SliderFloat("Opacity", &m.opacity, 0.0f, 1.0f);
    track();
    if (ImGui::TreeNode("Emission")) {
        changed |= ImGui::ColorEdit3("Color", &m.emissive.x, ImGuiColorEditFlags_Float);
        track();
        changed |= ImGui::DragFloat("Intensity", &m.emissiveIntensity, 0.05f, 0.0f, 10000.0f, "%.2f");
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Clear coat")) {
        changed |= ImGui::SliderFloat("Amount", &m.clearcoat, 0.0f, 1.0f);
        track();
        changed |= ImGui::SliderFloat("Roughness##cc", &m.clearcoatRoughness, 0.0f, 1.0f);
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Sheen")) {
        changed |= ImGui::ColorEdit3("Color##sheen", &m.sheenColor.x, ImGuiColorEditFlags_Float);
        track();
        changed |= ImGui::SliderFloat("Roughness##sheen", &m.sheenRoughness, 0.0f, 1.0f);
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Anisotropy")) {
        changed |= ImGui::SliderFloat("Strength", &m.anisotropy, -1.0f, 1.0f);
        track();
        changed |= ImGui::SliderFloat("Rotation", &m.anisotropyRotation, 0.0f, 1.0f);
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Subsurface")) {
        changed |= ImGui::SliderFloat("Amount##ss", &m.subsurface, 0.0f, 1.0f);
        track();
        changed |= ImGui::ColorEdit3("Scatter", &m.subsurfaceColor.x, ImGuiColorEditFlags_Float);
        track();
        changed |= ImGui::SliderFloat("Thickness", &m.thickness, 0.0f, 1.0f);
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Transmission")) {
        changed |= ImGui::SliderFloat("Amount##tr", &m.transmission, 0.0f, 1.0f);
        track();
        changed |= ImGui::SliderFloat("IOR", &m.ior, 1.0f, 2.5f);
        track();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Mapping")) {
        changed |= ImGui::DragFloat("UV scale", &m.uvScale, 0.01f, 0.01f, 1000.0f);
        track();
        changed |= ImGui::Checkbox("Triplanar", &m.triplanar);
        track();
        changed |= ImGui::SliderFloat("Normal strength", &m.normalStrength, 0.0f, 2.0f);
        track();
        changed |= ImGui::SliderFloat("Occlusion", &m.occlusion, 0.0f, 1.0f);
        track();
        changed |= ImGui::DragFloat4("Custom 0", &m.custom0.x, 0.01f);
        track();
        changed |= ImGui::DragFloat4("Custom 1", &m.custom1.x, 0.01f);
        track();
        ImGui::TreePop();
    }
    if (activated) before = engine.material(id);
    if (changed) engine.setMaterial(id, m);
    if (committed) {
        const Material old = before;
        const Material now = engine.material(id);
        ctx.history->record("Edit material", [&engine, id, now] { engine.setMaterial(id, now); }, [&engine, id, old] { engine.setMaterial(id, old); });
    }
    ImGui::PopID();
}

void Inspector::drawRender(EditorContext& ctx, EntityHandle e) {
    World& world = ctx.world;
    Engine& engine = ctx.engine;
    if (!ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const RenderableComponent r = world.getRenderable(e);
    const uint32_t mesh = world.getMesh(e);
    auto assign = [&](uint32_t newMesh, uint32_t material, uint32_t shader, float alpha, const char* label, bool meshChanged, bool materialChanged, bool shaderChanged, bool alphaChanged) {
        ctx.history->beginGroup(label);
        for (EntityHandle s : ctx.topLevelSelection()) {
            const RenderableComponent cur = world.getRenderable(s);
            ctx.history->setRender(s, meshChanged ? newMesh : world.getMesh(s), materialChanged ? material : cur.material_uuid, shaderChanged ? shader : cur.shaderId, alphaChanged ? alpha : cur.alpha);
        }
        ctx.history->endGroup();
    };

    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo("Mesh", meshLabel(engine, mesh).c_str())) {
        if (ImGui::Selectable("empty", mesh == NO_MESH)) assign(NO_MESH, 0, 0, 1.0f, "Set mesh", true, false, false, false);
        for (const std::string& name : engine.availableMeshes()) {
            if (ImGui::Selectable(name.c_str(), meshLabel(engine, mesh) == name)) {
                const uint32_t id = engine.loadMesh(name);
                assign(id, 0, 0, 1.0f, "Set mesh", true, false, false, false);
            }
        }
        ImGui::EndCombo();
    }

    const uint32_t material = r.material_uuid;
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo("Material", std::format("{}  #{}", engine.material(material).name, material).c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##mf", "filter", &m_materialFilter);
        if (ImGui::Selectable("+ New material")) {
            Material fresh = engine.material(material);
            fresh.name = std::format("Material {}", engine.materialCount());
            const uint32_t id = engine.createMaterial(fresh);
            if (id != 0) assign(0, id, 0, 1.0f, "New material", false, true, false, false);
        }
        for (uint32_t id = 0; id < engine.materialCount(); ++id) {
            const std::string label = std::format("{}  #{}", engine.material(id).name, id);
            if (!m_materialFilter.empty() && fuzzyScore(m_materialFilter, label) <= 0.0f) continue;
            if (ImGui::Selectable(label.c_str(), id == material)) assign(0, id, 0, 1.0f, "Assign material", false, true, false, false);
        }
        ImGui::EndCombo();
    }
    const std::vector<std::string> shaders = engine.shaderNames();
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo("Shader", r.shaderId < shaders.size() ? shaders[r.shaderId].c_str() : std::format("shader {}", r.shaderId).c_str())) {
        for (uint32_t id = 0; id < shaders.size(); ++id) {
            if (ImGui::Selectable(shaders[id].c_str(), id == r.shaderId)) assign(0, 0, id, 1.0f, "Set shader", false, false, true, false);
        }
        ImGui::EndCombo();
    }
    static float alphaBefore = 1.0f;
    float alpha = r.alpha;
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f)) {
        for (EntityHandle s : ctx.topLevelSelection()) world.setAlpha(s, alpha);
    }
    if (ImGui::IsItemActivated()) alphaBefore = r.alpha;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        for (EntityHandle s : ctx.topLevelSelection()) world.setAlpha(s, alphaBefore);
        assign(0, 0, 0, alpha, "Alpha", false, false, false, true);
    }
    if (ImGui::TreeNodeEx("Material##editor", 0, "Edit material  ·  %s", engine.material(material).name.c_str())) {
        if (material == 0) ImGui::TextDisabled("Default material: shared by every object without one");
        drawMaterial(ctx, material);
        ImGui::TreePop();
    }
}

void Inspector::drawComponents(EditorContext& ctx, EntityHandle e) {
    World& world = ctx.world;
    const EntityDescription description = world.describeEntity(e);
    if (!ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (const auto& [name, text] : description.components) {
        ImGui::PushID(name.c_str());
        const bool open = ImGui::TreeNodeEx("##component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap, "%s", name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        if (ImGui::SmallButton("x")) {
            const std::string before = text;
            const std::string component = name;
            world.removeComponentByName(e, component);
            ctx.history->record("Remove component", [&world, e, component] { world.removeComponentByName(e, component); }, [&world, e, component, before] { world.setComponentFromText(e, component, before); });
            if (open) ImGui::TreePop();
            ImGui::PopID();
            break;
        }
        if (open) {
            if (name == "Light") {
                if (LightComponent* light = world.get<LightComponent>(e)) {
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
                    ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100000.0f);
                    track();
                    if (light->type != LightComponent::Type::Directional) {
                        ImGui::DragFloat("Range", &light->range, 0.1f, 0.01f, 100000.0f);
                        track();
                    }
                    if (light->type == LightComponent::Type::Spot) {
                        ImGui::SliderFloat("Inner cone", &light->innerConeDegrees, 0.0f, 89.0f);
                        track();
                        ImGui::SliderFloat("Outer cone", &light->outerConeDegrees, 0.0f, 89.0f);
                        track();
                    }
                    ImGui::SliderFloat("Specular", &light->specular, 0.0f, 1.0f);
                    track();
                    if (committed) {
                        const LightComponent old = before;
                        const LightComponent now = *light;
                        before = now;
                        ctx.history->record("Edit light", [&world, e, now] { if (auto* l = world.get<LightComponent>(e)) *l = now; }, [&world, e, old] { if (auto* l = world.get<LightComponent>(e)) *l = old; });
                    }
                }
            } else if (name == "Camera") {
                if (CameraComponent* camera = world.get<CameraComponent>(e)) {
                    ImGui::SliderFloat("FOV", &camera->fov, 10.0f, 150.0f);
                    ImGui::DragFloat("Near", &camera->nearPlane, 0.01f, 0.001f, 100.0f);
                    ImGui::DragFloat("Far", &camera->farPlane, 1.0f, 1.0f, 1000000.0f);
                    const bool active = world.getActiveCamera() == e;
                    if (ImGui::Button(active ? "Active camera" : "Make active")) world.setActiveCamera(active ? NULL_ENTITY : e);
                }
            } else {
                const std::string key = name + "#" + std::to_string(e.index);
                if (m_componentEditing != key) m_componentBuffer = text;
                ImGui::SetNextItemWidth(-1.0f);
                const bool enter = ImGui::InputText("##text", &m_componentBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::IsItemActive()) m_componentEditing = key;
                if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
                    if (!ctx.history->setComponentText(e, name, m_componentBuffer)) ctx.toast(std::format("'{}' rejected that value", name), ToastKind::Error);
                    m_componentEditing.clear();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Serialized text; Enter applies (undoable)");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    for (const auto& [name, text] : description.scripts) ImGui::BulletText("script  %s", name.c_str());

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##add", "+ Add component")) {
        for (const std::string& name : world.componentNames()) {
            const bool present = std::any_of(description.components.begin(), description.components.end(), [&](const auto& c) { return c.first == name; });
            if (present) continue;
            if (ImGui::Selectable(name.c_str())) {
                if (world.addComponentByName(e, name)) {
                    const std::string component = name;
                    const std::string after = world.componentText(e, component).value_or("");
                    ctx.history->record("Add component", [&world, e, component, after] { world.setComponentFromText(e, component, after); }, [&world, e, component] { world.removeComponentByName(e, component); });
                }
            }
        }
        ImGui::EndCombo();
    }
}

void Inspector::draw(EditorContext& ctx) {
    if (!ctx.showInspector || ctx.zen) return;
    const EntityHandle e = ctx.primary();
    if (!ctx.world.isAlive(e)) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = 360.0f;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ctx.pinInspector) {
        glm::vec2 target(viewport->WorkPos.x + viewport->WorkSize.x - width - 16.0f, viewport->WorkPos.y + 130.0f);
        const glm::vec4 bounds = ctx.world.getWorldBounds(e);
        const glm::vec3 anchor = bounds.w > 0.0f ? glm::vec3(bounds) : ctx.world.getWorldPosition(e);
        if (const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(anchor)) {
            const bool onScreen = screen->x > 0.0f && screen->y > 0.0f && screen->x < viewport->WorkSize.x && screen->y < viewport->WorkSize.y;
            if (onScreen) {
                const float side = screen->x + 90.0f + width < viewport->WorkSize.x - 8.0f ? screen->x + 90.0f : screen->x - 90.0f - width;
                target = glm::vec2(std::clamp(side, 8.0f, viewport->WorkSize.x - width - 8.0f), std::clamp(screen->y - 140.0f, 8.0f, std::max(8.0f, viewport->WorkSize.y - m_lastHeight - 44.0f)));
            }
        }
        if (m_cardPos.x < 0.0f) m_cardPos = target;
        const float k = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 14.0f);
        m_cardPos += (target - m_cardPos) * k;
        ImGui::SetNextWindowPos(ImVec2(m_cardPos.x, m_cardPos.y), ImGuiCond_Always);
        m_setPos = m_cardPos;
    } else {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 12.0f, viewport->WorkPos.y + 12.0f), ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 120.0f), ImVec2(width * 1.6f, viewport->WorkSize.y - 60.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    const std::string title = ctx.selection.size() > 1 ? std::format("{} selected  ·  {}###Inspector", ctx.selection.size(), displayName(ctx.world, e)) : std::format("{}###Inspector", displayName(ctx.world, e));
    if (!ImGui::Begin(title.c_str(), &ctx.showInspector, flags | (ctx.pinInspector ? 0 : ImGuiWindowFlags_AlwaysAutoResize))) {
        ImGui::End();
        return;
    }
    if (!ctx.pinInspector) {
        const ImVec2 pos = ImGui::GetWindowPos();
        if ((std::abs(pos.x - m_setPos.x) > 2.0f || std::abs(pos.y - m_setPos.y) > 2.0f) && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsWindowFocused()) {
            ctx.pinInspector = true;
            ctx.toast("Card pinned. Ctrl+Shift+I to let it follow the selection again");
        }
    }
    m_lastHeight = ImGui::GetWindowHeight();
    ImGui::PushItemWidth(-90.0f);
    drawHeader(ctx, e);
    drawTransform(ctx, e);
    drawRender(ctx, e);
    drawComponents(ctx, e);
    ImGui::PopItemWidth();
    ImGui::End();
}
