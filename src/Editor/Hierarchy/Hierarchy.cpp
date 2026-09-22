module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

module Editor.hierarchy;

import Engine.input;

void Hierarchy::collectRows(World& world, size_t limit, std::vector<Row>& rows) const {
    std::vector<std::pair<EntityHandle, int>> stack;
    EntityHandle root = world.firstRoot();
    std::vector<EntityHandle> pendingSiblings;
    struct Frame {
        EntityHandle next;
        int depth;
    };
    std::vector<Frame> frames{{root, 0}};
    while (!frames.empty() && rows.size() < limit) {
        Frame& frame = frames.back();
        if (frame.next.isNull()) {
            frames.pop_back();
            continue;
        }
        const EntityHandle current = frame.next;
        const int depth = frame.depth;
        frame.next = world.nextSibling(current);
        const EntityHandle child = world.firstChild(current);
        rows.push_back({current, depth, !child.isNull()});
        if (!child.isNull() && m_expanded.count(current.index)) frames.push_back({child, depth + 1});
    }
}

long Hierarchy::indexOf(World& world, EntityHandle target) const {
    struct Frame {
        EntityHandle next;
    };
    std::vector<Frame> frames{{world.firstRoot()}};
    long index = 0;
    while (!frames.empty()) {
        Frame& frame = frames.back();
        if (frame.next.isNull()) {
            frames.pop_back();
            continue;
        }
        const EntityHandle current = frame.next;
        frame.next = world.nextSibling(current);
        if (current == target) return index;
        ++index;
        const EntityHandle child = world.firstChild(current);
        if (!child.isNull() && m_expanded.count(current.index)) frames.push_back({child});
    }
    return -1;
}

void Hierarchy::reveal(World& world, EntityHandle selected) {
    for (EntityHandle p = world.getParent(selected); !p.isNull(); p = world.getParent(p)) m_expanded.insert(p.index);
    const long index = indexOf(world, selected);
    if (index < 0) return;
    if (static_cast<size_t>(index) < m_offset) m_offset = static_cast<size_t>(index);
    if (static_cast<size_t>(index) >= m_offset + kRows) m_offset = static_cast<size_t>(index) - kRows + 1;
}

namespace {
constexpr float kPanelX = 900.0f;
constexpr float kHeaderY = 700.0f;
constexpr float kRowStart = 680.0f;
constexpr float kRowStep = 18.0f;
}

bool Hierarchy::insidePanel(Engine& engine, const glm::vec2& mouse) const {
    const float height = engine.windowSize().y;
    const float top = height - kHeaderY - 14.0f;
    const float bottom = height - (kRowStart - kRowStep * (static_cast<float>(kRows) - 1.0f)) + 6.0f;
    return mouse.x >= kPanelX && mouse.y >= top && mouse.y <= bottom;
}

int Hierarchy::rowAt(Engine& engine, const glm::vec2& mouse) const {
    if (!insidePanel(engine, mouse)) return -1;
    const float height = engine.windowSize().y;
    for (size_t i = 0; i < m_rowEntities.size(); ++i) {
        const float baseline = height - (kRowStart - kRowStep * static_cast<float>(i));
        if (mouse.y >= baseline - 13.0f && mouse.y <= baseline + 4.0f) return static_cast<int>(i);
    }
    return -1;
}

HierarchyAction Hierarchy::update(Engine& engine, EntityHandle& selected, bool inputEnabled) {
    World& world = engine.world();
    HierarchyAction action;
    if (selected != m_lastSelected) {
        m_lastSelected = selected;
        if (world.isAlive(selected)) reveal(world, selected);
    }

    std::vector<Row> rows;
    collectRows(world, m_offset + kRows, rows);
    const size_t first = std::min(m_offset, rows.size());

    long selectedRow = -1;
    for (size_t i = first; i < rows.size(); ++i) {
        if (rows[i].entity == selected) selectedRow = static_cast<long>(i);
    }

    if (!inputEnabled) {
        m_dragging = false;
    }
    if (inputEnabled && InputManager::IsKeyPressed(GLFW_KEY_PAGE_UP) && m_offset > 0) m_offset = m_offset > kRows ? m_offset - kRows : 0;
    if (inputEnabled && InputManager::IsKeyPressed(GLFW_KEY_PAGE_DOWN)) m_offset += kRows;

    if (inputEnabled && (InputManager::IsKeyPressed(GLFW_KEY_DOWN) || InputManager::IsKeyPressed(GLFW_KEY_UP))) {
        const long delta = InputManager::IsKeyPressed(GLFW_KEY_DOWN) ? 1 : -1;
        const long target = (selectedRow < 0 ? static_cast<long>(first) : selectedRow + delta);
        std::vector<Row> probe;
        collectRows(world, static_cast<size_t>(std::max(target, 0L)) + 1, probe);
        if (target >= 0 && static_cast<size_t>(target) < probe.size()) selected = probe[static_cast<size_t>(target)].entity;
    }
    if (inputEnabled && world.isAlive(selected)) {
        if (InputManager::IsKeyPressed(GLFW_KEY_RIGHT)) m_expanded.insert(selected.index);
        if (InputManager::IsKeyPressed(GLFW_KEY_LEFT)) m_expanded.erase(selected.index);
    }

    m_rowEntities.clear();
    m_shownFirst = first;
    for (size_t i = first; i < rows.size() && i < first + kRows; ++i) m_rowEntities.push_back(rows[i].entity);

    if (inputEnabled) {
        const glm::vec2 mouse = InputManager::GetMousePosition();
        if (InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) && insidePanel(engine, mouse)) {
            const int row = rowAt(engine, mouse);
            m_dragEntity = row >= 0 ? m_rowEntities[static_cast<size_t>(row)] : NULL_ENTITY;
            m_dragging = true;
            m_dragStart = mouse;
        }
        if (m_dragging) {
            action.consumedMouse = true;
            if (!InputManager::IsMouseButtonHeld(GLFW_MOUSE_BUTTON_LEFT) && !InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
                m_dragging = false;
                const int row = rowAt(engine, mouse);
                const bool moved = glm::length(mouse - m_dragStart) > 6.0f;
                if (world.isAlive(m_dragEntity)) {
                    if (!moved || (row >= 0 && m_rowEntities[static_cast<size_t>(row)] == m_dragEntity)) {
                        action.kind = HierarchyAction::Kind::Select;
                        action.entity = m_dragEntity;
                    } else if (row >= 0) {
                        const EntityHandle target = m_rowEntities[static_cast<size_t>(row)];
                        if (!world.isDescendantOf(target, m_dragEntity)) {
                            action.kind = HierarchyAction::Kind::Reparent;
                            action.entity = m_dragEntity;
                            action.newParent = target;
                        }
                    } else if (insidePanel(engine, mouse)) {
                        action.kind = HierarchyAction::Kind::Reparent;
                        action.entity = m_dragEntity;
                        action.newParent = NULL_ENTITY;
                    }
                }
                m_dragEntity = NULL_ENTITY;
            }
        } else if (insidePanel(engine, mouse)) {
            action.consumedMouse = true;
        }
    }

    float y = 700.0f;
    const float x = 900.0f;
    engine.AddText(m_dragging && world.isAlive(m_dragEntity) ? "Drop on a row to reparent, on this header for root" : "Hierarchy  (click, drag to reparent, arrows, PgUp/PgDn)", x, y, 0.45f, glm::vec3(1.0f, 0.9f, 0.2f));
    y -= 20.0f;
    for (size_t i = first; i < rows.size() && i < first + kRows; ++i) {
        const Row& row = rows[i];
        const std::string name = world.getName(row.entity).empty() ? std::format("#{}", row.entity.index) : world.getName(row.entity);
        const char marker = row.hasChildren ? (m_expanded.count(row.entity.index) ? '-' : '+') : ' ';
        const std::string text = std::format("{}{} {}", std::string(static_cast<size_t>(row.depth) * 2, ' '), marker, name);
        engine.AddText(text, x, y, 0.45f, row.entity == selected ? glm::vec3(1.0f, 0.9f, 0.2f) : glm::vec3(1.0f));
        y -= 18.0f;
    }
    return action;
}
