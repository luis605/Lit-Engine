module;

#include <cstdint>
#include <vector>
#include <stack>
#include <algorithm>
#include <stack>

export module Engine.Render.scenedatabase;

import Engine.Render.entity;
import Engine.Render.component;

export class SceneDatabase {
  public:
    std::vector<TransformComponent> transforms;
    std::vector<HierarchyComponent> hierarchies;
    std::vector<RenderableComponent> renderables;
    std::vector<Entity> sortedHierarchyList;
    bool m_isHierarchyDirty = true;
    uint32_t m_maxHierarchyDepth = 0;

    Entity createEntity() {
        transforms.emplace_back();
        hierarchies.emplace_back(HierarchyComponent{INVALID_ENTITY, 0});
        renderables.emplace_back();
        const auto entity = static_cast<Entity>(transforms.size() - 1);
        renderables.back().objectId = entity;
        m_isHierarchyDirty = true;
        return entity;
    }

    void updateHierarchy() {
        if (!m_isHierarchyDirty) {
            return;
        }

        if (transforms.empty()) {
            sortedHierarchyList.clear();
            m_maxHierarchyDepth = 0;
            return;
        }

        const size_t numEntities = transforms.size();
        if (sortedHierarchyList.capacity() < numEntities) {
            sortedHierarchyList.reserve(numEntities);
        }
        sortedHierarchyList.clear();

        std::vector<std::vector<Entity>> children(numEntities);
        std::vector<Entity> roots;
        roots.reserve(numEntities);

        for (Entity i = 0; i < numEntities; ++i) {
            hierarchies[i].level = 0;
            const auto& hier = hierarchies[i];
            if (hier.parent != INVALID_ENTITY && hier.parent < numEntities) {
                children[hier.parent].push_back(i);
            } else {
                roots.push_back(i);
            }
        }

        m_maxHierarchyDepth = 0;
        std::stack<std::pair<Entity, uint32_t>> stack;

        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            stack.push({*it, 0});
        }

        while (!stack.empty()) {
            auto [current, currentLevel] = stack.top();
            stack.pop();

            hierarchies[current].level = currentLevel;
            sortedHierarchyList.push_back(current);
            m_maxHierarchyDepth = std::max(m_maxHierarchyDepth, currentLevel);

            for (auto it = children[current].rbegin(); it != children[current].rend(); ++it) {
                stack.push({*it, currentLevel + 1});
            }
        }
        m_isHierarchyDirty = false;
    }
};
