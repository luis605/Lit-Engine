module;

#include <vector>
#include <stack>
#include <algorithm>

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

    Entity createEntity() {
        transforms.emplace_back();
        hierarchies.emplace_back(HierarchyComponent{INVALID_ENTITY});
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
            const auto& hier = hierarchies[i];
            if (hier.parent != INVALID_ENTITY && hier.parent < numEntities) {
                children[hier.parent].push_back(i);
            } else {
                roots.push_back(i);
            }
        }

        std::stack<Entity> stack;
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            stack.push(*it);
        }

        while (!stack.empty()) {
            Entity current = stack.top();
            stack.pop();
            sortedHierarchyList.push_back(current);

            for (auto it = children[current].rbegin(); it != children[current].rend(); ++it) {
                stack.push(*it);
            }
        }
        m_isHierarchyDirty = false;
    }
};
