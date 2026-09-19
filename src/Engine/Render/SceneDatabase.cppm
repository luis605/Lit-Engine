module;

#include <cstdint>
#include <vector>
#include <stack>
#include <algorithm>
#include <queue>

export module Engine.Render.scenedatabase;

import Engine.Render.entity;
import Engine.Render.component;

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

    Entity createEntity() {
        transforms.emplace_back();
        hierarchies.emplace_back(HierarchyComponent{INVALID_ENTITY, 0});
        renderables.emplace_back();
        const auto entity = static_cast<Entity>(transforms.size() - 1);
        renderables.back().objectId = entity;
        m_hierarchyVersion++;
        m_dataVersion++;
        return entity;
    }

    void markHierarchyDirty() { m_hierarchyVersion++; }
    void markDataDirty() { m_dataVersion++; }
    void markTransformsDirty(uint32_t count = 0) {
        m_transformVersion++;
        m_movingCount = count;
    }

    uint32_t m_dirtyEpoch = 1;
    std::vector<uint32_t> m_dirtyStamp;
    std::vector<Entity> m_dirtyList;

    void markEntityDirty(Entity e) {
        if (m_dirtyStamp.size() <= e) m_dirtyStamp.resize(transforms.size(), 0);
        if (m_dirtyStamp[e] != m_dirtyEpoch) {
            m_dirtyStamp[e] = m_dirtyEpoch;
            m_dirtyList.push_back(e);
        }
    }

    void clearDirty() {
        m_dirtyList.clear();
        m_dirtyEpoch++;
    }

    std::vector<uint32_t> m_childCounts;
    std::vector<uint32_t> m_childOffsets;
    std::vector<uint32_t> m_writeHeads;
    std::vector<Entity> m_flatChildren;
    std::vector<Entity> m_roots;
    std::vector<std::pair<Entity, uint32_t>> m_bfsQueue;

    void updateHierarchy() {
        if (transforms.empty()) {
            sortedHierarchyList.clear();
            m_levelCounts.clear();
            m_maxHierarchyDepth = 0;
            return;
        }

        const size_t numEntities = transforms.size();

        if (sortedHierarchyList.capacity() < numEntities) sortedHierarchyList.reserve(numEntities);
        sortedHierarchyList.clear();
        m_levelCounts.clear();

        if (m_childCounts.size() < numEntities) m_childCounts.resize(numEntities);
        if (m_childOffsets.size() < numEntities) m_childOffsets.resize(numEntities);
        if (m_flatChildren.size() < numEntities) m_flatChildren.resize(numEntities);

        std::fill(m_childCounts.begin(), m_childCounts.end(), 0);
        m_roots.clear();

        for (Entity i = 0; i < static_cast<Entity>(numEntities); ++i) {
            hierarchies[i].level = 0;
            const auto& hier = hierarchies[i];
            if (hier.parent != INVALID_ENTITY && hier.parent < numEntities) {
                m_childCounts[hier.parent]++;
            } else {
                m_roots.push_back(i);
            }
        }

        uint32_t currentOffset = 0;
        for (size_t i = 0; i < numEntities; ++i) {
            m_childOffsets[i] = currentOffset;
            currentOffset += m_childCounts[i];
        }

        if (m_flatChildren.size() < currentOffset) m_flatChildren.resize(currentOffset);

        if (m_writeHeads.size() < numEntities) m_writeHeads.resize(numEntities);

        std::copy(m_childOffsets.begin(), m_childOffsets.begin() + numEntities, m_writeHeads.begin());

        for (Entity i = 0; i < static_cast<Entity>(numEntities); ++i) {
            const auto& hier = hierarchies[i];
            if (hier.parent != INVALID_ENTITY && hier.parent < numEntities) { m_flatChildren[m_writeHeads[hier.parent]++] = i; }
        }

        m_maxHierarchyDepth = 0;

        m_bfsQueue.clear();
        for (auto root : m_roots) { m_bfsQueue.push_back({root, 0}); }

        size_t head = 0;
        while (head < m_bfsQueue.size()) {
            auto [current, currentLevel] = m_bfsQueue[head++];

            hierarchies[current].level = currentLevel;
            sortedHierarchyList.push_back(current);
            m_maxHierarchyDepth = std::max(m_maxHierarchyDepth, currentLevel);

            if (m_levelCounts.size() <= currentLevel) { m_levelCounts.resize(currentLevel + 1, 0); }
            m_levelCounts[currentLevel]++;

            uint32_t start = m_childOffsets[current];
            uint32_t end = m_writeHeads[current];

            for (uint32_t i = start; i < end; ++i) { m_bfsQueue.push_back({m_flatChildren[i], currentLevel + 1}); }
        }
    }
};