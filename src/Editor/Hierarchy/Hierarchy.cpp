module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <sstream>
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

constexpr size_t kMaxRows = 200000;
constexpr size_t kMaxResults = 5000;
constexpr size_t kSearchBudget = 300000;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void drawEye(ImDrawList* draw, ImVec2 center, float r, bool visible, ImU32 color) {
    draw->PathArcTo(ImVec2(center.x, center.y + r * 0.9f), r * 1.35f, -2.35f, -0.79f, 12);
    draw->PathArcTo(ImVec2(center.x, center.y - r * 0.9f), r * 1.35f, 0.79f, 2.35f, 12);
    draw->PathStroke(color, ImDrawFlags_Closed, 1.3f);
    if (visible) {
        draw->AddCircleFilled(center, r * 0.42f, color, 12);
    } else {
        draw->AddLine(ImVec2(center.x - r, center.y + r * 0.8f), ImVec2(center.x + r, center.y - r * 0.8f), color, 1.4f);
    }
}

}  // namespace

void Outliner::parseQuery() {
    m_query = Query{};
    std::istringstream in(m_search);
    std::string token;
    std::string text;
    while (in >> token) {
        const std::string t = lower(token);
        if (t.rfind("tag:", 0) == 0) {
            m_query.tag = token.substr(4);
        } else if (t.rfind("mesh:", 0) == 0) {
            m_query.mesh = lower(token.substr(5));
        } else if (t.rfind("layer:", 0) == 0) {
            m_query.layer = std::atoi(token.c_str() + 6);
        } else if (t == "vis:0" || t == "hidden") {
            m_query.visible = 0;
        } else if (t == "vis:1" || t == "visible") {
            m_query.visible = 1;
        } else if (t == "sel" || t == "selected") {
            m_query.selectedOnly = true;
        } else {
            if (!text.empty()) text += ' ';
            text += t;
        }
    }
    m_query.text = text;
    m_query.active = !m_search.empty();
}

bool Outliner::matches(EditorContext& ctx, EntityHandle e) const {
    World& world = ctx.world;
    if (m_query.selectedOnly && !ctx.selection.contains(e)) return false;
    if (m_query.visible >= 0 && world.isVisible(e) != (m_query.visible == 1)) return false;
    if (m_query.layer >= 0 && (world.getLayer(e) & (1u << m_query.layer)) == 0) return false;
    if (!m_query.tag.empty() && world.getTag(e) != m_query.tag) return false;
    if (!m_query.mesh.empty() && lower(meshLabel(ctx.engine, world.getMesh(e))).find(m_query.mesh) == std::string::npos) return false;
    if (!m_query.text.empty()) {
        const std::string name = lower(displayName(world, e));
        if (name.find(m_query.text) == std::string::npos) return false;
    }
    return true;
}

void Outliner::runSearch(EditorContext& ctx) {
    if (m_searchDone) return;
    World& world = ctx.world;
    const size_t slots = world.database().transforms.size();
    size_t budget = kSearchBudget;
    while (m_searchCursor < slots && budget-- > 0 && m_searchResults.size() < kMaxResults) {
        const EntityHandle e = world.handleOf(m_searchCursor++);
        if (!e.isNull() && matches(ctx, e)) m_searchResults.push_back(e);
    }
    if (m_searchCursor >= slots || m_searchResults.size() >= kMaxResults) m_searchDone = true;
}

void Outliner::rebuild(EditorContext& ctx) {
    World& world = ctx.world;
    m_rows.clear();
    m_truncated = false;
    struct Frame {
        EntityHandle next;
        uint16_t depth;
    };
    std::vector<Frame> frames{{world.firstRoot(), 0}};
    while (!frames.empty()) {
        if (m_rows.size() >= kMaxRows) {
            m_truncated = true;
            break;
        }
        Frame& frame = frames.back();
        if (frame.next.isNull()) {
            frames.pop_back();
            continue;
        }
        const EntityHandle current = frame.next;
        const uint16_t depth = frame.depth;
        frame.next = world.nextSibling(current);
        const EntityHandle child = world.firstChild(current);
        m_rows.push_back({current, depth, !child.isNull()});
        if (!child.isNull() && m_expanded.count(current.index)) frames.push_back({child, static_cast<uint16_t>(depth + 1)});
    }
    m_builtStructure = world.database().m_structureVersion;
    m_builtExpansion = m_expansionVersion;
    m_lastBuild = ImGui::GetTime();
}

void Outliner::reveal(EditorContext& ctx, EntityHandle e) {
    if (!ctx.world.isAlive(e)) return;
    for (EntityHandle p = ctx.world.getParent(e); !p.isNull(); p = ctx.world.getParent(p)) m_expanded.insert(p.index);
    ++m_expansionVersion;
    m_scrollTo = e;
}

void Outliner::drawRow(EditorContext& ctx, const Row& row, int index) {
    World& world = ctx.world;
    const EntityHandle e = row.entity;
    if (!world.isAlive(e)) {
        ImGui::TextDisabled("(destroyed)");
        return;
    }
    ImGui::PushID(static_cast<int>(e.index));
    ImGui::PushID(static_cast<int>(e.generation));
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float indent = static_cast<float>(row.depth) * 14.0f;
    const bool selected = ctx.selection.contains(e);
    const bool primary = ctx.primary() == e;
    const bool visible = world.isVisible(e);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::SetNextItemAllowOverlap();
    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, rowHeight))) {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_renaming = e;
            m_renameBuffer = world.getName(e);
            m_renameFocus = true;
        } else if (io.KeyCtrl) {
            ctx.selection.toggle(e);
        } else if (io.KeyShift && world.isAlive(m_rangeAnchor)) {
            int anchor = -1;
            for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
                if (m_rows[i].entity == m_rangeAnchor) anchor = i;
            }
            if (anchor >= 0 && !m_query.active) {
                for (int i = std::min(anchor, index); i <= std::max(anchor, index); ++i) ctx.selection.add(m_rows[i].entity);
                ctx.selection.remove(e);
                ctx.selection.add(e);
            } else {
                ctx.selection.add(e);
            }
        } else {
            ctx.selection.set(e);
            m_rangeAnchor = e;
        }
    }
    if (ImGui::IsItemHovered()) ctx.hovered = e;

    if (ImGui::BeginDragDropSource()) {
        if (!ctx.selection.contains(e)) ctx.selection.set(e);
        ImGui::SetDragDropPayload("LIT_ENTITY", &e, sizeof(EntityHandle));
        ImGui::Text("Move %zu under...", ctx.selection.size());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("LIT_ENTITY")) {
            ctx.history->beginGroup("Reparent");
            for (EntityHandle moved : ctx.topLevelSelection()) {
                if (moved != e && !world.isDescendantOf(e, moved)) ctx.history->setParent(moved, e);
            }
            ctx.history->endGroup();
            m_expanded.insert(e.index);
            ++m_expansionVersion;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("row menu")) {
        if (!ctx.selection.contains(e)) ctx.selection.set(e);
        if (ImGui::MenuItem("Rename", "F2")) {
            m_renaming = e;
            m_renameBuffer = world.getName(e);
            m_renameFocus = true;
        }
        if (ImGui::MenuItem("Focus", "F")) {
            const glm::vec4 b = world.getWorldBounds(e);
            flyTo(ctx, b.w > 0.0f ? b : glm::vec4(world.getWorldPosition(e), 1.0f));
        }
        if (ImGui::MenuItem(visible ? "Hide" : "Show", "H")) ctx.history->setVisible(e, !visible);
        if (ImGui::MenuItem("Select children")) {
            for (EntityHandle c : world.getChildren(e)) ctx.selection.add(c);
        }
        if (ImGui::MenuItem("Unparent", "Alt+P", false, !world.getParent(e).isNull())) ctx.history->setParent(e, NULL_ENTITY);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            ctx.history->beginGroup("Delete");
            for (EntityHandle d : ctx.topLevelSelection()) ctx.history->destroy(d);
            ctx.history->endGroup();
            ctx.selection.clear();
        }
        ImGui::EndPopup();
    }

    const float arrowX = start.x + indent + 4.0f;
    const float centerY = start.y + rowHeight * 0.5f;
    if (row.hasChildren && !m_query.active) {
        const bool open = m_expanded.count(e.index) > 0;
        ImGui::SetCursorScreenPos(ImVec2(arrowX - 2.0f, start.y));
        if (ImGui::InvisibleButton("##arrow", ImVec2(14.0f, rowHeight))) {
            if (open) {
                m_expanded.erase(e.index);
            } else {
                m_expanded.insert(e.index);
            }
            ++m_expansionVersion;
        }
        ImGui::RenderArrow(draw, ImVec2(arrowX, centerY - ImGui::GetFontSize() * 0.36f), IM_COL32(170, 176, 188, 255), open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
    }

    const float textX = arrowX + 16.0f;
    const ImU32 textColor = !visible ? IM_COL32(120, 124, 132, 255) : primary ? IM_COL32(255, 200, 110, 255) : IM_COL32(230, 232, 236, 255);
    if (m_renaming == e) {
        ImGui::SetCursorScreenPos(ImVec2(textX, start.y));
        ImGui::SetNextItemWidth(width - (textX - start.x) - 30.0f);
        if (m_renameFocus) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocus = false;
        }
        const bool done = ImGui::InputText("##rename", &m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (done || ImGui::IsItemDeactivated()) {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape)) ctx.history->setName(e, m_renameBuffer);
            m_renaming = NULL_ENTITY;
        }
    } else {
        const std::string name = displayName(world, e);
        draw->AddText(ImVec2(textX, centerY - ImGui::GetFontSize() * 0.5f), textColor, name.c_str());
        const uint32_t mesh = world.getMesh(e);
        const std::string meta = mesh == NO_MESH ? std::string("empty") : meshLabel(ctx.engine, mesh);
        const ImVec2 metaSize = ImGui::CalcTextSize(meta.c_str());
        const float metaX = start.x + width - metaSize.x - 30.0f;
        if (metaX > textX + ImGui::CalcTextSize(name.c_str()).x + 12.0f) draw->AddText(ImVec2(metaX, centerY - ImGui::GetFontSize() * 0.5f), IM_COL32(110, 116, 128, 255), meta.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(start.x + width - 22.0f, start.y));
    if (ImGui::InvisibleButton("##eye", ImVec2(20.0f, rowHeight))) ctx.history->setVisible(e, !visible);
    const bool eyeHover = ImGui::IsItemHovered();
    drawEye(draw, ImVec2(start.x + width - 12.0f, centerY), 5.0f, visible, eyeHover ? IM_COL32(255, 190, 90, 255) : visible ? IM_COL32(170, 176, 188, 200) : IM_COL32(110, 114, 122, 200));
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rowHeight + ImGui::GetStyle().ItemSpacing.y * 0.25f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    ImGui::PopID();
}

void Outliner::draw(EditorContext& ctx) {
    if (!ctx.showOutliner || ctx.zen) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, viewport->WorkSize.y * 0.62f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);
    ctx.outlinerFocused = false;
    if (!ImGui::Begin("Scene", &ctx.showOutliner, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    ctx.outlinerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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

    World& world = ctx.world;
    const bool stale = m_builtStructure != world.database().m_structureVersion || m_builtExpansion != m_expansionVersion;
    if (stale && (m_builtExpansion != m_expansionVersion || ImGui::GetTime() - m_lastBuild > 0.2)) rebuild(ctx);
    if (m_query.active) runSearch(ctx);

    const size_t total = world.aliveCount();
    if (m_query.active) {
        ImGui::TextDisabled("%zu match%s%s", m_searchResults.size(), m_searchResults.size() == 1 ? "" : "es", m_searchDone ? "" : "  (scanning...)");
    } else {
        ImGui::TextDisabled("%zu entities%s", total, m_truncated ? "  (first 200k rows, use filter)" : "");
    }
    ImGui::SameLine();
    const float buttonWidth = ImGui::CalcTextSize("Collapse").x + 12.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonWidth);
    if (ImGui::SmallButton("Collapse")) {
        m_expanded.clear();
        ++m_expansionVersion;
    }
    ImGui::Separator();

    ImGui::BeginChild("rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const int count = static_cast<int>(m_query.active ? m_searchResults.size() : m_rows.size());
    const float rowStride = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 0.25f;
    if (!m_scrollTo.isNull() && !m_query.active) {
        for (int i = 0; i < count; ++i) {
            if (m_rows[i].entity == m_scrollTo) {
                const float y = static_cast<float>(i) * rowStride;
                if (y < ImGui::GetScrollY() || y > ImGui::GetScrollY() + ImGui::GetWindowHeight() - rowStride) ImGui::SetScrollY(y - ImGui::GetWindowHeight() * 0.4f);
                m_scrollTo = NULL_ENTITY;
                break;
            }
        }
        if (m_builtExpansion == m_expansionVersion) m_scrollTo = NULL_ENTITY;
    }
    ImGuiListClipper clipper;
    clipper.Begin(count, rowStride);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            if (m_query.active) {
                const EntityHandle e = m_searchResults[i];
                drawRow(ctx, Row{e, 0, false}, i);
            } else {
                drawRow(ctx, m_rows[i], i);
            }
        }
    }
    clipper.End();

    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && !m_query.active && count > 0) {
        int current = -1;
        for (int i = 0; i < count; ++i) {
            if (m_rows[i].entity == ctx.primary()) {
                current = i;
                break;
            }
        }
        int next = current;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(current + 1, count - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(current - 1, 0);
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow) && m_rows[current].hasChildren) {
            m_expanded.insert(m_rows[current].entity.index);
            ++m_expansionVersion;
        }
        if (current >= 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (m_expanded.erase(m_rows[current].entity.index)) {
                ++m_expansionVersion;
            } else if (!world.getParent(m_rows[current].entity).isNull()) {
                ctx.selection.set(world.getParent(m_rows[current].entity));
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
        if (current >= 0 && (ImGui::IsKeyPressed(ImGuiKey_F2) || ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            m_renaming = m_rows[current].entity;
            m_renameBuffer = world.getName(m_renaming);
            m_renameFocus = true;
        }
    }

    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 24.0f)));
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("LIT_ENTITY")) {
            ctx.history->beginGroup("Unparent");
            for (EntityHandle moved : ctx.topLevelSelection()) ctx.history->setParent(moved, NULL_ENTITY);
            ctx.history->endGroup();
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) ctx.selection.clear();
    ImGui::EndChild();
    ImGui::End();
}

