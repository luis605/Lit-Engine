module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

module Editor.inspector;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.camera;
import Engine.glm;

namespace {

constexpr float AXIS_LABEL_WIDTH = 64.0f;
constexpr float AXIS_STRIPE_WIDTH = 3.0f;
constexpr float AXIS_STRIPE_ROUNDING = 3.0f;
constexpr float POSITION_DRAG_SPEED = 0.05f;
constexpr float ROTATION_DRAG_SPEED = 0.5f;
constexpr float SCALE_DRAG_SPEED = 0.01f;
constexpr float SCALE_EPSILON = 1e-6f;
constexpr float EULER_GIMBAL_LIMIT = 0.9999f;
constexpr float NAME_RESERVED_WIDTH = 110.0f;
constexpr float PIN_BUTTON_RESERVED_WIDTH = 36.0f;
constexpr float RESET_RESERVED_WIDTH = 110.0f;
constexpr float CARD_WIDTH = 340.0f;
constexpr float CARD_MIN_HEIGHT = 80.0f;
constexpr float CARD_MAX_HEIGHT = 560.0f;
constexpr float CARD_MIN_MAX_HEIGHT = 240.0f;
constexpr float CARD_MAX_HEIGHT_FRACTION = 0.55f;
constexpr float PINNED_HEIGHT_MARGIN = 60.0f;
constexpr float CARD_FOLLOW_RATE = 14.0f;
constexpr float CARD_BACKGROUND_ALPHA = 0.92f;
constexpr float CARD_DRAG_THRESHOLD = 2.0f;
constexpr float DOCK_MARGIN = 12.0f;
constexpr float DOCK_PANEL_GAP = 10.0f;
constexpr float LEADER_LINE_THICKNESS = 1.5f;
constexpr float LEADER_DOT_RADIUS = 3.5f;
constexpr ImU32 LEADER_LINE_COLOR = IM_COL32(255, 184, 72, 150);
constexpr ImU32 LEADER_DOT_COLOR = IM_COL32(255, 184, 72, 200);

glm::quat eulerToQuat(const glm::vec3& degrees) {
    const glm::vec3 radians = glm::radians(degrees);
    return glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 quatToEuler(const glm::quat& rotation) {
    const glm::mat3 matrix = glm::mat3_cast(glm::normalize(rotation));
    const float sinePitch = std::clamp(-matrix[0][2], -1.0f, 1.0f);
    const float pitch = std::asin(sinePitch);
    float roll = 0.0f;
    float yaw = 0.0f;
    if (std::abs(sinePitch) < EULER_GIMBAL_LIMIT) {
        roll = std::atan2(matrix[1][2], matrix[2][2]);
        yaw = std::atan2(matrix[0][1], matrix[0][0]);
    } else {
        roll = std::atan2(-matrix[2][1], matrix[1][1]);
    }
    return glm::degrees(glm::vec3(roll, pitch, yaw));
}

bool axisDrag(const char* label, glm::vec3& value, float speed, const char* format, bool& activated, bool& deactivated) {
    ImGui::PushID(label);
    bool changed = false;
    activated = false;
    deactivated = false;
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(AXIS_LABEL_WIDTH);
    const float fieldWidth = (fullWidth - AXIS_LABEL_WIDTH - innerSpacing * 2.0f) / 3.0f;
    const ImU32 colors[3] = {IM_COL32(230, 80, 80, 255), IM_COL32(110, 210, 90, 255), IM_COL32(80, 140, 240, 255)};
    const char* ids[3] = {"##x", "##y", "##z"};
    for (int axis = 0; axis < 3; ++axis) {
        if (axis > 0) ImGui::SameLine(0.0f, innerSpacing);
        ImGui::SetNextItemWidth(fieldWidth);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        changed |= ImGui::DragFloat(ids[axis], &value[axis], speed, 0.0f, 0.0f, format);
        activated |= ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + AXIS_STRIPE_WIDTH, origin.y + ImGui::GetFrameHeight()), colors[axis], AXIS_STRIPE_ROUNDING, ImDrawFlags_RoundCornersLeft);
    }
    ImGui::PopID();
    return changed;
}

glm::mat4 identityTransform() { return glm::mat4(1.0f); }

}

void Inspector::beginTransformEdit(EditorContext& ctx) {
    if (m_editing) return;
    m_editing = true;
    m_editStart.clear();
    for (EntityHandle entity : ctx.topLevelSelection()) m_editStart.push_back({entity, ctx.world.getLocalMatrix(entity)});
}

void Inspector::endTransformEdit(EditorContext& ctx, const char* label) {
    if (!m_editing) return;
    m_editing = false;
    ctx.history->beginGroup(label);
    for (const Snapshot& snapshot : m_editStart) {
        if (ctx.world.isAlive(snapshot.entity)) ctx.history->commitLocalMatrix(snapshot.entity, snapshot.local, ctx.world.getLocalMatrix(snapshot.entity));
    }
    ctx.history->endGroup();
    m_editStart.clear();
}

void Inspector::drawTransform(EditorContext& ctx, EntityHandle entity) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;
    World& world = ctx.world;
    glm::vec3 position = world.getPosition(entity);
    glm::vec3 scale = world.getScale(entity);
    glm::vec3 euler = quatToEuler(world.getRotation(entity));
    bool activated = false;
    bool deactivated = false;
    const std::vector<EntityHandle> selection = ctx.topLevelSelection();

    const glm::vec3 oldPosition = position;
    if (axisDrag("Position", position, POSITION_DRAG_SPEED, "%.3f", activated, deactivated)) {
        const glm::vec3 delta = position - oldPosition;
        for (EntityHandle selected : selection) world.setPosition(selected, selected == entity ? position : world.getPosition(selected) + delta);
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Move");

    const glm::vec3 oldEuler = euler;
    if (axisDrag("Rotation", euler, ROTATION_DRAG_SPEED, "%.1f°", activated, deactivated)) {
        const glm::quat next = eulerToQuat(euler);
        const glm::quat delta = next * glm::inverse(eulerToQuat(oldEuler));
        for (EntityHandle selected : selection) world.setRotation(selected, selected == entity ? next : glm::normalize(delta * world.getRotation(selected)));
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Rotate");

    const glm::vec3 oldScale = scale;
    if (axisDrag("Scale", scale, SCALE_DRAG_SPEED, "%.3f", activated, deactivated)) {
        glm::vec3 ratio(1.0f);
        for (int axis = 0; axis < 3; ++axis) ratio[axis] = std::abs(oldScale[axis]) > SCALE_EPSILON ? scale[axis] / oldScale[axis] : 1.0f;
        for (EntityHandle selected : selection) world.setScale(selected, selected == entity ? scale : world.getScale(selected) * ratio);
    }
    if (activated) beginTransformEdit(ctx);
    if (deactivated) endTransformEdit(ctx, "Scale");

    const glm::vec3 worldPosition = world.getWorldPosition(entity);
    ImGui::TextDisabled("World  %.2f  %.2f  %.2f", worldPosition.x, worldPosition.y, worldPosition.z);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - RESET_RESERVED_WIDTH);
    if (ImGui::SmallButton("Reset")) {
        ctx.history->beginGroup("Reset transform");
        for (EntityHandle selected : selection) ctx.history->setLocalMatrix(selected, identityTransform());
        ctx.history->endGroup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(std::format("{} {} {}", position.x, position.y, position.z).c_str());
}

void Inspector::drawHeader(EditorContext& ctx, EntityHandle entity) {
    drawNameRow(ctx, entity);
    World& world = ctx.world;
    bool visible = world.isVisible(entity);
    if (ImGui::Checkbox("Visible", &visible)) {
        ctx.history->beginGroup("Visibility");
        for (EntityHandle selected : ctx.topLevelSelection()) ctx.history->setVisible(selected, visible);
        ctx.history->endGroup();
    }
    drawParentRow(ctx, entity);
    drawTagAndLayers(ctx, entity);
}

void Inspector::drawNameRow(EditorContext& ctx, EntityHandle entity) {
    World& world = ctx.world;
    if (m_bufferEntity != entity || !ImGui::IsAnyItemActive()) {
        m_nameBuffer = world.getName(entity);
        m_tagBuffer = world.getTag(entity);
        m_bufferEntity = entity;
    }
    if (m_focusName) {
        ImGui::SetKeyboardFocusHere();
        m_focusName = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - NAME_RESERVED_WIDTH);
    if (ImGui::InputTextWithHint("##name", displayName(world, entity).c_str(), &m_nameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) ctx.history->setName(entity, m_nameBuffer);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.history->setName(entity, m_nameBuffer);
    ImGui::SameLine();
    ImGui::TextDisabled("#%u", entity.index);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - PIN_BUTTON_RESERVED_WIDTH);
    if (ImGui::SmallButton(ctx.pinInspector ? "Float" : "Pin")) {
        ctx.pinInspector = !ctx.pinInspector;
        m_dock = ctx.pinInspector;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(ctx.pinInspector ? "Follow the selection again (Ctrl+Shift+I)" : "Dock the card to the right edge (Ctrl+Shift+I)");
}

void Inspector::drawParentRow(EditorContext& ctx, EntityHandle entity) {
    const EntityHandle parent = ctx.world.getParent(entity);
    if (parent.isNull()) return;
    ImGui::SameLine();
    ImGui::TextDisabled("in");
    ImGui::SameLine();
    if (ImGui::SmallButton(displayName(ctx.world, parent).c_str())) ctx.selection.set(parent);
    ImGui::SameLine();
    if (ImGui::SmallButton("Unparent")) ctx.history->setParent(entity, NULL_ENTITY);
}

void Inspector::drawTagAndLayers(EditorContext& ctx, EntityHandle entity) {
    if (!ImGui::TreeNode("Tag, layers, occlusion")) return;
    World& world = ctx.world;
    bool occluder = (world.getRenderFlags(entity) & RENDER_NO_OCCLUDER) == 0;
    if (ImGui::Checkbox("Occluder", &occluder)) {
        std::vector<EntityHandle> targets = ctx.topLevelSelection();
        auto applyOccluder = [&world, targets](bool on) {
            for (EntityHandle target : targets) world.setRenderFlags(target, RENDER_NO_OCCLUDER, !on);
        };
        applyOccluder(occluder);
        ctx.history->record("Occluder", [applyOccluder, occluder] { applyOccluder(occluder); }, [applyOccluder, occluder] { applyOccluder(!occluder); });
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: drawn after the occlusion buffer so it never hides other objects");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##tag", "tag", &m_tagBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) ctx.history->setTag(entity, m_tagBuffer);
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.history->setTag(entity, m_tagBuffer);
    drawLayerButtons(ctx, entity);
    ImGui::TreePop();
}

void Inspector::drawLayerButtons(EditorContext& ctx, EntityHandle entity) {
    constexpr int LAYER_COUNT = 8;
    constexpr float LAYER_BUTTON_SPACING = 3.0f;
    const ImVec4 activeColor(1.0f, 0.69f, 0.26f, 0.55f);
    World& world = ctx.world;
    const uint32_t layer = world.getLayer(entity);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Layers");
    for (int bit = 0; bit < LAYER_COUNT; ++bit) {
        ImGui::SameLine(0.0f, LAYER_BUTTON_SPACING);
        const bool on = (layer & (1u << bit)) != 0;
        ImGui::PushID(bit);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? activeColor : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        if (ImGui::SmallButton(std::format("{}", bit).c_str())) {
            const uint32_t next = layer ^ (1u << bit);
            std::vector<EntityHandle> targets = ctx.topLevelSelection();
            std::vector<uint32_t> before;
            before.reserve(targets.size());
            for (EntityHandle target : targets) before.push_back(world.getLayer(target));
            for (EntityHandle target : targets) world.setLayer(target, next);
            ctx.history->record(
                "Layer", [&world, targets, next] { for (EntityHandle target : targets) world.setLayer(target, next); },
                [&world, targets, before] {
                    for (size_t index = 0; index < targets.size(); ++index) world.setLayer(targets[index], before[index]);
                });
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void Inspector::positionWindow(EditorContext& ctx, EntityHandle entity, float width, float maxHeight) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!ctx.pinInspector) {
        const glm::vec2 target = placeCard(ctx, entity, width, std::min(m_lastHeight, maxHeight));
        if (m_cardPos.x < 0.0f) m_cardPos = target;
        const float follow = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * CARD_FOLLOW_RATE);
        m_cardPos += (target - m_cardPos) * follow;
        ImGui::SetNextWindowPos(ImVec2(m_cardPos.x, m_cardPos.y), ImGuiCond_Always);
        m_setPos = m_cardPos;
        return;
    }
    if (m_dock) {
        float right = viewport->WorkPos.x + viewport->WorkSize.x - DOCK_MARGIN;
        const float middle = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
        for (const glm::vec4& panel : ctx.panels) {
            if (panel.x + panel.z * 0.5f > middle) right = std::min(right, panel.x - DOCK_PANEL_GAP);
        }
        ImGui::SetNextWindowPos(ImVec2(right - width, ctx.rightColumnTop), ImGuiCond_Always);
        m_dock = false;
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - DOCK_MARGIN, ctx.rightColumnTop), ImGuiCond_FirstUseEver);
}

void Inspector::drawLeaderLine() {
    if (!m_leader) return;
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 from(std::clamp(m_leader->x, pos.x, pos.x + size.x), std::clamp(m_leader->y, pos.y, pos.y + size.y));
    const ImVec2 target(m_leader->x, m_leader->y);
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddLine(from, target, LEADER_LINE_COLOR, LEADER_LINE_THICKNESS);
    drawList->AddCircleFilled(target, LEADER_DOT_RADIUS, LEADER_DOT_COLOR);
}

void Inspector::draw(EditorContext& ctx) {
    m_sections.clear();
    if (!ctx.showInspector || ctx.zen) return;
    const EntityHandle entity = ctx.primary();
    if (!ctx.world.isAlive(entity)) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float maxHeight = ctx.pinInspector ? viewport->WorkSize.y - PINNED_HEIGHT_MARGIN : std::min(CARD_MAX_HEIGHT, std::max(CARD_MIN_MAX_HEIGHT, viewport->WorkSize.y * CARD_MAX_HEIGHT_FRACTION));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    positionWindow(ctx, entity, CARD_WIDTH, maxHeight);
    ImGui::SetNextWindowSizeConstraints(ImVec2(CARD_WIDTH, CARD_MIN_HEIGHT), ImVec2(CARD_WIDTH, maxHeight));
    ImGui::SetNextWindowBgAlpha(CARD_BACKGROUND_ALPHA);
    const std::string name = displayName(ctx.world, entity);
    const std::string title = ctx.selection.size() > 1 ? std::format("{} selected  ·  {}###Inspector", ctx.selection.size(), name) : std::format("{}###Inspector", name);
    if (!ImGui::Begin(title.c_str(), &ctx.showInspector, flags)) {
        ImGui::End();
        return;
    }
    if (!ctx.pinInspector) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const bool moved = std::abs(pos.x - m_setPos.x) > CARD_DRAG_THRESHOLD || std::abs(pos.y - m_setPos.y) > CARD_DRAG_THRESHOLD;
        if (moved && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsWindowFocused()) {
            ctx.pinInspector = true;
            ctx.toast("Card pinned. Ctrl+Shift+I to let it follow the selection again");
        }
    }
    m_lastHeight = ImGui::GetWindowHeight();
    if (!ctx.pinInspector) drawLeaderLine();

    World& world = ctx.world;
    const bool hasLight = world.get<LightComponent>(entity) != nullptr;
    const bool hasCamera = world.get<CameraComponent>(entity) != nullptr;
    const bool gadget = (hasLight || hasCamera) && world.getMesh(entity) == NO_MESH;
    ImGui::PushItemWidth(-90.0f);
    drawHeader(ctx, entity);
    m_sections = "header";
    drawTransform(ctx, entity);
    m_sections += ",transform";
    if (!gadget) {
        drawRender(ctx, entity);
        m_sections += ",render";
    }
    drawComponents(ctx, entity, gadget);
    m_sections += ",components";
    ImGui::PopItemWidth();
    ImGui::End();
}
