module;

#include <algorithm>
#include <cstdint>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.Jobs;
import Engine.glm;

namespace {

constexpr size_t kPartitionCount = 64;
constexpr uint64_t kPartitionHashMultiplier = 0x9E3779B97F4A7C15ull;
constexpr int kPartitionHashShift = 58;
constexpr uint8_t kStateCell = 1;
constexpr uint8_t kStateLarge = 2;
constexpr uint8_t kStateLargeRemoved = 3;

size_t partitionOf(uint64_t cell) { return static_cast<size_t>((cell * kPartitionHashMultiplier) >> kPartitionHashShift); }

}

struct World::SpatialBatch {
    World& world;
    const std::vector<Entity>& pending;
    const std::vector<SpatialEntry>& fresh;
    std::vector<std::vector<uint32_t>> removals{kPartitionCount};
    std::vector<std::vector<uint32_t>> inserts{kPartitionCount};
    std::vector<uint32_t> largeInserts;
    std::vector<uint32_t> largeCleared;
    std::vector<std::vector<uint64_t>> emptiedCells{kPartitionCount};
    std::vector<std::vector<uint64_t>> missingCells{kPartitionCount};
    bool largeRemoved = false;

    SpatialBatch(World& target, const std::vector<Entity>& pendingEntities, const std::vector<SpatialEntry>& freshEntries) : world(target), pending(pendingEntities), fresh(freshEntries) {}

    void classify() {
        for (size_t position = 0; position < pending.size(); ++position) {
            const Entity index = pending[position];
            if (index >= world.m_alive.size()) continue;
            SpatialEntry& entry = world.m_spatial[index];
            const SpatialEntry& next = fresh[position];
            if (world.refreshSpatialInPlace(entry, next)) continue;
            const uint32_t position32 = static_cast<uint32_t>(position);
            if (entry.state == kStateCell) removals[partitionOf(entry.cell)].push_back(position32);
            if (entry.state == kStateLarge) {
                largeRemoved = true;
                entry.state = kStateLargeRemoved;
                if (next.state == 0) largeCleared.push_back(position32);
            }
            if (next.state == kStateCell) inserts[partitionOf(next.cell)].push_back(position32);
            if (next.state == kStateLarge) largeInserts.push_back(position32);
            if (entry.state == 0) entry = next;
        }
        if (largeRemoved) {
            std::erase_if(world.m_largeEntities, [this](Entity entity) { return world.m_spatial[entity].state == kStateLargeRemoved; });
        }
        for (uint32_t position : largeCleared) world.m_spatial[pending[position]] = fresh[position];
    }

    void removeFromCell(size_t partition, uint32_t position) {
        const Entity index = pending[position];
        SpatialEntry& entry = world.m_spatial[index];
        const auto cellIt = world.m_cells.find(entry.cell);
        if (cellIt != world.m_cells.end()) {
            auto& bucket = cellIt->second;
            if (entry.slot < bucket.size() && bucket[entry.slot] == index) {
                const Entity moved = bucket.back();
                bucket[entry.slot] = moved;
                world.m_spatial[moved].slot = entry.slot;
                bucket.pop_back();
            }
            if (bucket.empty()) emptiedCells[partition].push_back(entry.cell);
        }
        entry.state = 0;
    }

    void removeAndFindMissing() {
        world.m_jobs->parallelFor(kPartitionCount, 1, [&](size_t begin, size_t end) {
            for (size_t partition = begin; partition < end; ++partition) {
                for (uint32_t position : removals[partition]) removeFromCell(partition, position);
                for (uint32_t position : inserts[partition]) {
                    if (world.m_cells.find(fresh[position].cell) == world.m_cells.end()) missingCells[partition].push_back(fresh[position].cell);
                }
            }
        });
        for (const auto& cells : missingCells) {
            for (uint64_t cell : cells) {
                if (world.m_cells.try_emplace(cell).second) world.noteCellAdded(cell);
                world.growCellBounds(cell);
            }
        }
    }

    void insertIntoCells() {
        std::vector<float> partitionRadii(kPartitionCount, 0.0f);
        world.m_jobs->parallelFor(kPartitionCount, 1, [&](size_t begin, size_t end) {
            for (size_t partition = begin; partition < end; ++partition) {
                for (uint32_t position : inserts[partition]) {
                    const Entity index = pending[position];
                    SpatialEntry& entry = world.m_spatial[index];
                    entry = fresh[position];
                    auto& bucket = world.m_cells.find(entry.cell)->second;
                    entry.slot = static_cast<uint32_t>(bucket.size());
                    bucket.push_back(index);
                    partitionRadii[partition] = std::max(partitionRadii[partition], entry.radius);
                }
            }
        });
        for (float radius : partitionRadii) world.m_maxSmallRadius = std::max(world.m_maxSmallRadius, radius);
    }

    void finish() {
        for (const auto& cells : emptiedCells) {
            for (uint64_t cell : cells) {
                const auto cellIt = world.m_cells.find(cell);
                if (cellIt != world.m_cells.end() && cellIt->second.empty()) {
                    world.noteCellRemoved(cell);
                    world.m_cells.erase(cellIt);
                }
            }
        }
        for (uint32_t position : largeInserts) {
            world.m_spatial[pending[position]] = fresh[position];
            world.m_largeEntities.push_back(pending[position]);
        }
    }
};

void World::applySpatialUpdatesParallel(const std::vector<Entity>& pending, const std::vector<SpatialEntry>& fresh) {
    SpatialBatch batch(*this, pending, fresh);
    batch.classify();
    batch.removeAndFindMissing();
    batch.insertIntoCells();
    batch.finish();
}
