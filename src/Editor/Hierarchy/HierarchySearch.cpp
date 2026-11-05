module;

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

module Editor.hierarchy;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.Render.entity;

namespace {

constexpr size_t MAX_ROWS = 200000;
constexpr size_t MAX_SEARCH_RESULTS = 5000;
constexpr size_t SEARCH_BUDGET_PER_FRAME = 300000;
constexpr int LAYER_BIT_COUNT = 32;

std::string lower(std::string text) {
    for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

}

void Outliner::parseQuery() {
    m_query = Query{};
    std::istringstream stream(m_search);
    std::string token;
    std::string text;
    while (stream >> token) {
        const std::string lowered = lower(token);
        if (lowered.rfind("tag:", 0) == 0) {
            m_query.tag = token.substr(4);
        } else if (lowered.rfind("mesh:", 0) == 0) {
            m_query.mesh = lower(token.substr(5));
        } else if (lowered.rfind("layer:", 0) == 0) {
            m_query.layer = std::atoi(token.c_str() + 6);
        } else if (lowered == "vis:0" || lowered == "hidden") {
            m_query.visible = 0;
        } else if (lowered == "vis:1" || lowered == "visible") {
            m_query.visible = 1;
        } else if (lowered == "sel" || lowered == "selected") {
            m_query.selectedOnly = true;
        } else {
            if (!text.empty()) text += ' ';
            text += lowered;
        }
    }
    m_query.text = text;
    m_query.active = !m_search.empty();
}

bool Outliner::matches(EditorContext& ctx, EntityHandle entity) const {
    World& world = ctx.world;
    if (m_query.selectedOnly && !ctx.selection.contains(entity)) return false;
    if (m_query.visible >= 0 && world.isVisible(entity) != (m_query.visible == 1)) return false;
    if (m_query.layer >= 0 && (m_query.layer >= LAYER_BIT_COUNT || (world.getLayer(entity) & (1u << m_query.layer)) == 0)) return false;
    if (!m_query.tag.empty() && world.getTag(entity) != m_query.tag) return false;
    if (!m_query.mesh.empty() && lower(meshLabel(ctx.engine, world.getMesh(entity))).find(m_query.mesh) == std::string::npos) return false;
    if (!m_query.text.empty() && lower(displayName(world, entity)).find(m_query.text) == std::string::npos) return false;
    return true;
}

void Outliner::runSearch(EditorContext& ctx) {
    if (m_searchDone) return;
    World& world = ctx.world;
    const size_t slots = world.database().transforms.size();
    size_t budget = SEARCH_BUDGET_PER_FRAME;
    while (m_searchCursor < slots && budget-- > 0 && m_searchResults.size() < MAX_SEARCH_RESULTS) {
        const EntityHandle entity = world.handleOf(m_searchCursor++);
        if (!entity.isNull() && matches(ctx, entity)) m_searchResults.push_back(entity);
    }
    if (m_searchCursor >= slots || m_searchResults.size() >= MAX_SEARCH_RESULTS) m_searchDone = true;
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
        if (m_rows.size() >= MAX_ROWS) {
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
        if (!child.isNull() && m_expanded.contains(current.index)) frames.push_back({child, static_cast<uint16_t>(depth + 1)});
    }
    m_builtStructure = world.database().m_structureVersion;
    m_builtExpansion = m_expansionVersion;
    m_lastBuild = ImGui::GetTime();
}

void Outliner::reveal(EditorContext& ctx, EntityHandle entity) {
    if (!ctx.world.isAlive(entity)) return;
    for (EntityHandle ancestor = ctx.world.getParent(entity); !ancestor.isNull(); ancestor = ctx.world.getParent(ancestor)) m_expanded.insert(ancestor.index);
    ++m_expansionVersion;
    m_scrollTo = entity;
}
