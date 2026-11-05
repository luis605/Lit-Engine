module;

#include <cstdint>
#include <vector>
#include <algorithm>
#include <span>

export module Engine.Render.scenedatabase;

import Engine.Render.entity;
import Engine.Render.component;

export struct DirtySet {
    std::vector<Entity> list;

    void mark(Entity entity) {
        if (m_stamp.size() <= entity) m_stamp.resize(static_cast<size_t>(entity) + 1, 0);
        if (m_stamp[entity] == m_epoch) return;
        m_stamp[entity] = m_epoch;
        list.push_back(entity);
    }

    void truncate(size_t count) {
        std::erase_if(list, [count](Entity entity) { return static_cast<size_t>(entity) >= count; });
        if (m_stamp.size() > count) m_stamp.resize(count);
    }

    void reset() {
        list.clear();
        m_stamp.clear();
        m_epoch = 1;
    }

    void clear() {
        list.clear();
        if (++m_epoch == 0) {
            std::fill(m_stamp.begin(), m_stamp.end(), 0u);
            m_epoch = 1;
        }
    }

   private:
    std::vector<uint32_t> m_stamp;
    uint32_t m_epoch = 1;
};

export class SceneDatabase {
   public:
    std::vector<TransformComponent> transforms;
    std::vector<HierarchyComponent> hierarchies;
    std::vector<RenderableComponent> renderables;
    std::vector<Entity> sortedHierarchyList;
    std::vector<uint32_t> m_levelCounts;
    uint64_t m_hierarchyVersion = 1;
    uint64_t m_dataVersion = 1;
    uint64_t m_transformVersion = 1;
    uint32_t m_movingCount = 0;
    uint32_t m_maxHierarchyDepth = 0;

    std::vector<Entity> firstChild;
    std::vector<Entity> nextSibling;
    uint64_t m_structureVersion = 1;

    Entity createEntity() {
        transforms.emplace_back();
        hierarchies.emplace_back(HierarchyComponent{INVALID_ENTITY, 0});
        renderables.emplace_back();
        firstChild.push_back(INVALID_ENTITY);
        nextSibling.push_back(INVALID_ENTITY);
        const auto entity = static_cast<Entity>(transforms.size() - 1);
        renderables.back().objectId = entity;
        m_structureVersion++;
        return entity;
    }

    template <class Visitor>
    void forEachChild(Entity entity, Visitor&& visitor) const {
        if (entity >= firstChild.size()) return;
        for (Entity child = firstChild[entity]; child != INVALID_ENTITY; child = nextSibling[child]) visitor(child);
    }

    [[nodiscard]] bool hasChildren(Entity entity) const { return entity < firstChild.size() && firstChild[entity] != INVALID_ENTITY; }

    void markHierarchyDirty() {
        m_hierarchyVersion++;
        m_structureVersion++;
    }
    void markStructureChanged() { m_structureVersion++; }
    void markDataDirty() { m_dataVersion++; }
    void markTransformsDirty(uint32_t count = 0) {
        m_transformVersion++;
        m_movingCount = count;
    }

    DirtySet transformDirty;
    DirtySet renderableDirty;
    DirtySet structureDirty;

    void updateHierarchy() {
        const auto entityCount = static_cast<Entity>(hierarchies.size());
        sortedHierarchyList.clear();
        sortedHierarchyList.reserve(entityCount);
        for (Entity entity = 0; entity < entityCount; ++entity) {
            const Entity parent = hierarchies[entity].parent;
            if (parent != INVALID_ENTITY && parent < entityCount) continue;
            hierarchies[entity].level = 0;
            sortedHierarchyList.push_back(entity);
        }
        for (size_t head = 0; head < sortedHierarchyList.size() && sortedHierarchyList.size() <= entityCount; ++head) {
            const Entity entity = sortedHierarchyList[head];
            forEachChild(entity, [&](Entity child) {
                hierarchies[child].level = hierarchies[entity].level + 1;
                sortedHierarchyList.push_back(child);
            });
        }

        m_levelCounts.clear();
        for (Entity entity : sortedHierarchyList) {
            const uint32_t level = hierarchies[entity].level;
            if (m_levelCounts.size() <= level) m_levelCounts.resize(level + 1, 0);
            m_levelCounts[level]++;
        }
        m_maxHierarchyDepth = m_levelCounts.empty() ? 0 : static_cast<uint32_t>(m_levelCounts.size() - 1);
    }
};