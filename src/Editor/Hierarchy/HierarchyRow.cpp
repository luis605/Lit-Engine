module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

module Editor.hierarchy;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

namespace {

constexpr float INDENT_PER_DEPTH = 14.0f;
constexpr float ARROW_OFFSET = 4.0f;
constexpr float ARROW_BUTTON_WIDTH = 14.0f;
constexpr float TEXT_OFFSET_AFTER_ARROW = 16.0f;
constexpr float RIGHT_RESERVED_WIDTH = 30.0f;
constexpr float EYE_BUTTON_WIDTH = 20.0f;
constexpr float EYE_BUTTON_OFFSET = 22.0f;
constexpr float EYE_CENTER_OFFSET = 12.0f;
constexpr float EYE_RADIUS = 5.0f;
constexpr float META_MIN_GAP = 12.0f;
constexpr float ROW_SPACING_FRACTION = 0.25f;

void drawEye(ImDrawList* drawList, ImVec2 center, float radius, bool visible, ImU32 color) {
    drawList->PathArcTo(ImVec2(center.x, center.y + radius * 0.9f), radius * 1.35f, -2.35f, -0.79f, 12);
    drawList->PathArcTo(ImVec2(center.x, center.y - radius * 0.9f), radius * 1.35f, 0.79f, 2.35f, 12);
    drawList->PathStroke(color, ImDrawFlags_Closed, 1.3f);
    if (visible) {
        drawList->AddCircleFilled(center, radius * 0.42f, color, 12);
    } else {
        drawList->AddLine(ImVec2(center.x - radius, center.y + radius * 0.8f), ImVec2(center.x + radius, center.y - radius * 0.8f), color, 1.4f);
    }
}

void focusEntity(EditorContext& ctx, EntityHandle entity) {
    const glm::vec4 bounds = ctx.world.getWorldBounds(entity);
    flyTo(ctx, bounds.w > 0.0f ? bounds : glm::vec4(ctx.world.getWorldPosition(entity), 1.0f));
}

}

void Outliner::beginRename(EditorContext& ctx, EntityHandle entity) {
    m_renaming = entity;
    m_renameBuffer = ctx.world.getName(entity);
    m_renameFocus = true;
}

void Outliner::handleRowSelection(EditorContext& ctx, EntityHandle entity, int index) {
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        beginRename(ctx, entity);
        return;
    }
    if (io.KeyCtrl) {
        ctx.selection.toggle(entity);
        return;
    }
    if (!io.KeyShift || !ctx.world.isAlive(m_rangeAnchor)) {
        ctx.selection.set(entity);
        m_rangeAnchor = entity;
        return;
    }
    int anchor = -1;
    for (int rowIndex = 0; rowIndex < static_cast<int>(m_rows.size()); ++rowIndex) {
        if (m_rows[rowIndex].entity == m_rangeAnchor) anchor = rowIndex;
    }
    if (anchor < 0 || m_query.active) {
        ctx.selection.add(entity);
        return;
    }
    for (int rowIndex = std::min(anchor, index); rowIndex <= std::max(anchor, index); ++rowIndex) ctx.selection.add(m_rows[rowIndex].entity);
    ctx.selection.remove(entity);
    ctx.selection.add(entity);
}

void Outliner::handleRowDragDrop(EditorContext& ctx, EntityHandle entity) {
    if (ImGui::BeginDragDropSource()) {
        if (!ctx.selection.contains(entity)) ctx.selection.set(entity);
        ImGui::SetDragDropPayload("LIT_ENTITY", &entity, sizeof(EntityHandle));
        ImGui::Text("Move %zu under...", ctx.selection.size());
        ImGui::EndDragDropSource();
    }
    if (!ImGui::BeginDragDropTarget()) return;
    if (ImGui::AcceptDragDropPayload("LIT_ENTITY")) {
        ctx.history->beginGroup("Reparent");
        for (EntityHandle moved : ctx.topLevelSelection()) {
            if (moved != entity && !ctx.world.isDescendantOf(entity, moved)) ctx.history->setParent(moved, entity);
        }
        ctx.history->endGroup();
        m_expanded.insert(entity.index);
        ++m_expansionVersion;
    }
    ImGui::EndDragDropTarget();
}

void Outliner::drawRowContextMenu(EditorContext& ctx, EntityHandle entity, bool visible) {
    if (!ImGui::BeginPopupContextItem("row menu")) return;
    World& world = ctx.world;
    if (!ctx.selection.contains(entity)) ctx.selection.set(entity);
    if (ImGui::MenuItem("Rename", "F2")) beginRename(ctx, entity);
    if (ImGui::MenuItem("Focus", "F")) focusEntity(ctx, entity);
    if (ImGui::MenuItem(visible ? "Hide" : "Show", "H")) ctx.history->setVisible(entity, !visible);
    if (ImGui::MenuItem("Select children")) {
        for (EntityHandle child : world.getChildren(entity)) ctx.selection.add(child);
    }
    if (ImGui::MenuItem("Unparent", "Alt+P", false, !world.getParent(entity).isNull())) ctx.history->setParent(entity, NULL_ENTITY);
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del")) {
        ctx.history->beginGroup("Delete");
        for (EntityHandle doomed : ctx.topLevelSelection()) ctx.history->destroy(doomed);
        ctx.history->endGroup();
        ctx.selection.clear();
    }
    ImGui::EndPopup();
}

void Outliner::drawRowArrow(EntityHandle entity, float arrowX, float startY, float rowHeight, float centerY) {
    const bool open = m_expanded.contains(entity.index);
    ImGui::SetCursorScreenPos(ImVec2(arrowX - 2.0f, startY));
    if (ImGui::InvisibleButton("##arrow", ImVec2(ARROW_BUTTON_WIDTH, rowHeight))) {
        if (open) {
            m_expanded.erase(entity.index);
        } else {
            m_expanded.insert(entity.index);
        }
        ++m_expansionVersion;
    }
    ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImVec2(arrowX, centerY - ImGui::GetFontSize() * 0.36f), IM_COL32(170, 176, 188, 255), open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
}

void Outliner::drawRowLabel(EditorContext& ctx, EntityHandle entity, const ImVec2& start, float width, float textX, float centerY, bool primary, bool visible) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (m_renaming == entity) {
        ImGui::SetCursorScreenPos(ImVec2(textX, start.y));
        ImGui::SetNextItemWidth(width - (textX - start.x) - RIGHT_RESERVED_WIDTH);
        if (m_renameFocus) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocus = false;
        }
        const bool done = ImGui::InputText("##rename", &m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (done || ImGui::IsItemDeactivated()) {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape)) ctx.history->setName(entity, m_renameBuffer);
            m_renaming = NULL_ENTITY;
        }
        return;
    }
    const ImU32 textColor = !visible ? IM_COL32(120, 124, 132, 255) : primary ? IM_COL32(255, 200, 110, 255) : IM_COL32(230, 232, 236, 255);
    const float textY = centerY - ImGui::GetFontSize() * 0.5f;
    const std::string name = displayName(ctx.world, entity);
    drawList->AddText(ImVec2(textX, textY), textColor, name.c_str());
    const uint32_t mesh = ctx.world.getMesh(entity);
    const std::string meta = mesh == NO_MESH ? std::string("empty") : meshLabel(ctx.engine, mesh);
    const float metaX = start.x + width - ImGui::CalcTextSize(meta.c_str()).x - RIGHT_RESERVED_WIDTH;
    if (metaX > textX + ImGui::CalcTextSize(name.c_str()).x + META_MIN_GAP) drawList->AddText(ImVec2(metaX, textY), IM_COL32(110, 116, 128, 255), meta.c_str());
}

void Outliner::drawRow(EditorContext& ctx, const Row& row, int index) {
    World& world = ctx.world;
    const EntityHandle entity = row.entity;
    if (!world.isAlive(entity)) {
        ImGui::TextDisabled("(destroyed)");
        return;
    }
    ImGui::PushID(static_cast<int>(entity.index));
    ImGui::PushID(static_cast<int>(entity.generation));
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool visible = world.isVisible(entity);

    ImGui::SetNextItemAllowOverlap();
    if (ImGui::Selectable("##row", ctx.selection.contains(entity), ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, rowHeight))) handleRowSelection(ctx, entity, index);
    if (ImGui::IsItemHovered()) ctx.hovered = entity;
    handleRowDragDrop(ctx, entity);
    drawRowContextMenu(ctx, entity, visible);

    const float arrowX = start.x + static_cast<float>(row.depth) * INDENT_PER_DEPTH + ARROW_OFFSET;
    const float centerY = start.y + rowHeight * 0.5f;
    if (row.hasChildren && !m_query.active) drawRowArrow(entity, arrowX, start.y, rowHeight, centerY);
    drawRowLabel(ctx, entity, start, width, arrowX + TEXT_OFFSET_AFTER_ARROW, centerY, ctx.primary() == entity, visible);

    ImGui::SetCursorScreenPos(ImVec2(start.x + width - EYE_BUTTON_OFFSET, start.y));
    if (ImGui::InvisibleButton("##eye", ImVec2(EYE_BUTTON_WIDTH, rowHeight))) ctx.history->setVisible(entity, !visible);
    const bool eyeHover = ImGui::IsItemHovered();
    const ImU32 eyeColor = eyeHover ? IM_COL32(255, 190, 90, 255) : visible ? IM_COL32(170, 176, 188, 200) : IM_COL32(110, 114, 122, 200);
    drawEye(ImGui::GetWindowDrawList(), ImVec2(start.x + width - EYE_CENTER_OFFSET, centerY), EYE_RADIUS, visible, eyeColor);
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rowHeight + ImGui::GetStyle().ItemSpacing.y * ROW_SPACING_FRACTION));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    ImGui::PopID();
}
