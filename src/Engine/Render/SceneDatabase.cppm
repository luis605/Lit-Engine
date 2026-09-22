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

    void mark(Entity e) {
        if (m_stamp.size() <= e) m_stamp.resize(static_cast<size_t>(e) + 1, 0);
        if (m_stamp[e] == m_epoch) return;
        m_stamp[e] = m_epoch;
        list.push_back(e);
    }

    void truncate(size_t count) {
        std::erase_if(list, [count](Entity e) { return e >= count; });
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

    template <class Fn>
    void forEachChild(Entity e, Fn&& fn) const {
        if (e >= firstChild.size()) return;
        for (Entity child = firstChild[e]; child != INVALID_ENTITY; child = nextSibling[child]) fn(child);
    }

    [[nodiscard]] bool hasChildren(Entity e) const { return e < firstChild.size() && firstChild[e] != INVALID_ENTITY; }

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
        for (Entity e = 0; e < entityCount; ++e) {
            const Entity parent = hierarchies[e].parent;
            if (parent != INVALID_ENTITY && parent < entityCount) continue;
            hierarchies[e].level = 0;
            sortedHierarchyList.push_back(e);
        }
        for (size_t head = 0; head < sortedHierarchyList.size() && sortedHierarchyList.size() <= entityCount; ++head) {
            const Entity e = sortedHierarchyList[head];
            forEachChild(e, [&](Entity child) {
                hierarchies[child].level = hierarchies[e].level + 1;
                sortedHierarchyList.push_back(child);
            });
        }

        m_levelCounts.clear();
        for (Entity e : sortedHierarchyList) {
            const uint32_t level = hierarchies[e].level;
            if (m_levelCounts.size() <= level) m_levelCounts.resize(level + 1, 0);
            m_levelCounts[level]++;
        }
        m_maxHierarchyDepth = m_levelCounts.empty() ? 0 : static_cast<uint32_t>(m_levelCounts.size() - 1);
    }
};