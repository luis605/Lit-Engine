module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

module Editor.hierarchy;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr float WINDOW_MARGIN = 10.0f;
constexpr float WINDOW_WIDTH = 300.0f;
constexpr float WINDOW_HEIGHT_FRACTION = 0.62f;
constexpr float WINDOW_BACKGROUND_ALPHA = 0.90f;
constexpr double REBUILD_INTERVAL_SECONDS = 0.2;
constexpr float ROW_SPACING_FRACTION = 0.25f;
constexpr float SCROLL_TARGET_FRACTION = 0.4f;
constexpr float MIN_DROP_ZONE_HEIGHT = 24.0f;
constexpr float COLLAPSE_BUTTON_PADDING = 12.0f;

}

void Outliner::drawHeaderControls(EditorContext& ctx) {
    World& world = ctx.world;
    const bool structureStale = m_builtStructure != world.database().m_structureVersion;
    const bool expansionStale = m_builtExpansion != m_expansionVersion;
    if ((structureStale || expansionStale) && (expansionStale || ImGui::GetTime() - m_lastBuild > REBUILD_INTERVAL_SECONDS)) rebuild(ctx);
    if (m_query.active) runSearch(ctx);

    if (m_query.active) {
        ImGui::TextDisabled("%zu match%s%s", m_searchResults.size(), m_searchResults.size() == 1 ? "" : "es", m_searchDone ? "" : "  (scanning...)");
    } else {
        ImGui::TextDisabled("%zu entities%s", world.aliveCount(), m_truncated ? "  (first 200k rows, use filter)" : "");
    }
    ImGui::SameLine();
    const float buttonWidth = ImGui::CalcTextSize("Collapse").x + COLLAPSE_BUTTON_PADDING;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonWidth);
    if (ImGui::SmallButton("Collapse")) {
        m_expanded.clear();
        ++m_expansionVersion;
    }
    ImGui::Separator();
}

void Outliner::scrollToTarget(int count, float rowStride) {
    if (m_scrollTo.isNull() || m_query.active) return;
    for (int rowIndex = 0; rowIndex < count; ++rowIndex) {
        if (m_rows[rowIndex].entity != m_scrollTo) continue;
        const float targetY = static_cast<float>(rowIndex) * rowStride;
        if (targetY < ImGui::GetScrollY() || targetY > ImGui::GetScrollY() + ImGui::GetWindowHeight() - rowStride) ImGui::SetScrollY(targetY - ImGui::GetWindowHeight() * SCROLL_TARGET_FRACTION);
        m_scrollTo = NULL_ENTITY;
        break;
    }
    if (m_builtExpansion == m_expansionVersion) m_scrollTo = NULL_ENTITY;
}

void Outliner::drawRows(EditorContext& ctx, int count, float rowStride) {
    ImGuiListClipper clipper;
    clipper.Begin(count, rowStride);
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            if (m_query.active) {
                drawRow(ctx, Row{m_searchResults[rowIndex], 0, false}, rowIndex);
            } else {
                drawRow(ctx, m_rows[rowIndex], rowIndex);
            }
        }
    }
    clipper.End();
}

void Outliner::handleKeyboard(EditorContext& ctx, int count) {
    if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput || m_query.active || count <= 0) return;
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
    const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
    const bool rename = ImGui::IsKeyPressed(ImGuiKey_F2) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (!down && !up && !right && !left && !rename) return;

    World& world = ctx.world;
    const EntityHandle primary = ctx.primary();
    int current = -1;
    for (int rowIndex = 0; rowIndex < count; ++rowIndex) {
        if (m_rows[rowIndex].entity == primary) {
            current = rowIndex;
            break;
        }
    }
    int next = current;
    if (down) next = std::min(current + 1, count - 1);
    if (up) next = std::max(current - 1, 0);
    if (current >= 0 && right && m_rows[current].hasChildren) {
        m_expanded.insert(m_rows[current].entity.index);
        ++m_expansionVersion;
    }
    if (current >= 0 && left) {
        const EntityHandle parent = world.getParent(m_rows[current].entity);
        if (m_expanded.erase(m_rows[current].entity.index)) {
            ++m_expansionVersion;
        } else if (!parent.isNull()) {
            ctx.selection.set(parent);
            m_scrollTo = ctx.primary();
        }
    }
    if (next != current && next >= 0) {
        if (ImGui::GetIO().KeyShift) {
            ctx.selection.add(m_rows[next].entity);
        } else {
            ctx.selection.set(m_rows[next].entity);
        }
        m_scrollTo = m_rows[next].entity;
        m_rangeAnchor = m_rows[next].entity;
    }
    if (current >= 0 && rename) beginRename(ctx, m_rows[current].entity);
}

void Outliner::drawRootDropTarget(EditorContext& ctx) {
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, MIN_DROP_ZONE_HEIGHT)));
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("LIT_ENTITY")) {
            ctx.history->beginGroup("Unparent");
            for (EntityHandle moved : ctx.topLevelSelection()) ctx.history->setParent(moved, NULL_ENTITY);
            ctx.history->endGroup();
        }
        ImGui::EndDragDropTarget();
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemClicked() && !io.KeyCtrl && !io.KeyShift) ctx.selection.clear();
}

void Outliner::draw(EditorContext& ctx) {
    if (!ctx.showOutliner || ctx.zen) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + WINDOW_MARGIN, viewport->WorkPos.y + WINDOW_MARGIN), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(WINDOW_WIDTH, viewport->WorkSize.y * WINDOW_HEIGHT_FRACTION), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(WINDOW_BACKGROUND_ALPHA);
    ctx.outlinerFocused = false;
    if (!ImGui::Begin("Scene", &ctx.showOutliner, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    ctx.outlinerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ctx.panels.emplace_back(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

    if (m_focusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##search", "Filter   name  tag:x  mesh:cube  layer:2  hidden  sel", &m_search)) {
        parseQuery();
        m_searchResults.clear();
        m_searchCursor = 0;
        m_searchDone = !m_query.active;
    }
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_search.clear();
        parseQuery();
        m_searchDone = true;
    }

    drawHeaderControls(ctx);

    ImGui::BeginChild("rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const int count = static_cast<int>(m_query.active ? m_searchResults.size() : m_rows.size());
    const float rowStride = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * ROW_SPACING_FRACTION;
    scrollToTarget(count, rowStride);
    drawRows(ctx, count, rowStride);
    handleKeyboard(ctx, count);
    drawRootDropTarget(ctx);
    ImGui::EndChild();
    ImGui::End();
}
