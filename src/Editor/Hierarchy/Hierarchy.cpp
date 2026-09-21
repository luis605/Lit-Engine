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

void Hierarchy::update(Engine& engine, EntityHandle& selected) {
    World& world = engine.world();
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

    if (InputManager::IsKeyPressed(GLFW_KEY_PAGE_UP) && m_offset > 0) m_offset = m_offset > kRows ? m_offset - kRows : 0;
    if (InputManager::IsKeyPressed(GLFW_KEY_PAGE_DOWN)) m_offset += kRows;

    if (InputManager::IsKeyPressed(GLFW_KEY_DOWN) || InputManager::IsKeyPressed(GLFW_KEY_UP)) {
        const long delta = InputManager::IsKeyPressed(GLFW_KEY_DOWN) ? 1 : -1;
        const long target = (selectedRow < 0 ? static_cast<long>(first) : selectedRow + delta);
        std::vector<Row> probe;
        collectRows(world, static_cast<size_t>(std::max(target, 0L)) + 1, probe);
        if (target >= 0 && static_cast<size_t>(target) < probe.size()) selected = probe[static_cast<size_t>(target)].entity;
    }
    if (world.isAlive(selected)) {
        if (InputManager::IsKeyPressed(GLFW_KEY_RIGHT)) m_expanded.insert(selected.index);
        if (InputManager::IsKeyPressed(GLFW_KEY_LEFT)) m_expanded.erase(selected.index);
    }

    float y = 700.0f;
    const float x = 900.0f;
    engine.AddText("Hierarchy  (Up/Down select, Left/Right fold, PgUp/PgDn scroll)", x, y, 0.45f, glm::vec3(1.0f, 0.9f, 0.2f));
    y -= 20.0f;
    for (size_t i = first; i < rows.size() && i < first + kRows; ++i) {
        const Row& row = rows[i];
        const std::string name = world.getName(row.entity).empty() ? std::format("#{}", row.entity.index) : world.getName(row.entity);
        const char marker = row.hasChildren ? (m_expanded.count(row.entity.index) ? '-' : '+') : ' ';
        const std::string text = std::format("{}{} {}", std::string(static_cast<size_t>(row.depth) * 2, ' '), marker, name);
        engine.AddText(text, x, y, 0.45f, row.entity == selected ? glm::vec3(1.0f, 0.9f, 0.2f) : glm::vec3(1.0f));
        y -= 18.0f;
    }
}
