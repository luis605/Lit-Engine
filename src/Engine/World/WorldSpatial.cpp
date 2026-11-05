module;

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.Profiler;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr float kOrbitReach = 3.4642f;
constexpr float kLargeRadiusCells = 4.0f;
constexpr uint64_t kCoordinateMask = 0x1FFFFF;
constexpr int kYShift = 21;
constexpr int kXShift = 42;
constexpr int kMaxKeyedCell = 1000000;
constexpr size_t kParallelComputeThreshold = 4096;
constexpr size_t kParallelComputeGrain = 8192;
constexpr size_t kParallelApplyThreshold = 65536;

enum SpatialState : uint8_t { StateAbsent = 0, StateCell = 1, StateLarge = 2 };

}

uint64_t World::spatialKey(int cellX, int cellY, int cellZ) const {
    return ((static_cast<uint64_t>(cellX) & kCoordinateMask) << kXShift) | ((static_cast<uint64_t>(cellY) & kCoordinateMask) << kYShift) | (static_cast<uint64_t>(cellZ) & kCoordinateMask);
}

void World::growCellBounds(uint64_t key) {
    const int cellX = decodeCellCoordinate(key, kXShift);
    const int cellY = decodeCellCoordinate(key, kYShift);
    const int cellZ = decodeCellCoordinate(key, 0);
    m_cellBounds = {std::min(m_cellBounds[0], cellX), std::min(m_cellBounds[1], cellY), std::min(m_cellBounds[2], cellZ), std::max(m_cellBounds[3], cellX), std::max(m_cellBounds[4], cellY), std::max(m_cellBounds[5], cellZ)};
}

void World::resetCellBounds() {
    constexpr int kUnset = std::numeric_limits<int>::max();
    m_cellBounds = {kUnset, kUnset, kUnset, -kUnset, -kUnset, -kUnset};
}

void World::setSpatialCellSize(float size) {
    if (!(size > 0.0f)) return;
    m_cellSize = size;
    m_cells.clear();
    m_coarseCells.clear();
    m_largeEntities.clear();
    m_spatial.clear();
    m_spatialQueued.clear();
    m_spatialPending.clear();
    m_spatialRoots.clear();
    m_spatialRootQueued.clear();
    m_maxSmallRadius = 0.0f;
    resetCellBounds();
    m_spatialActive = false;
}

void World::noteCellAdded(uint64_t key) {
    ++m_coarseCells[spatialKey(decodeCellCoordinate(key, kXShift) >> kCoarseShift, decodeCellCoordinate(key, kYShift) >> kCoarseShift, decodeCellCoordinate(key, 0) >> kCoarseShift)];
}

void World::noteCellRemoved(uint64_t key) {
    const auto coarseIt = m_coarseCells.find(spatialKey(decodeCellCoordinate(key, kXShift) >> kCoarseShift, decodeCellCoordinate(key, kYShift) >> kCoarseShift, decodeCellCoordinate(key, 0) >> kCoarseShift));
    if (coarseIt != m_coarseCells.end() && --coarseIt->second == 0) m_coarseCells.erase(coarseIt);
}

bool World::coarseBlockEmpty(int coarseX, int coarseY, int coarseZ) const {
    if (m_coarseCells.find(spatialKey(coarseX, coarseY, coarseZ)) != m_coarseCells.end()) return false;
    for (int offsetX = -1; offsetX <= 1; ++offsetX) {
        for (int offsetY = -1; offsetY <= 1; ++offsetY) {
            for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
                if (m_coarseCells.find(spatialKey(coarseX + offsetX, coarseY + offsetY, coarseZ + offsetZ)) != m_coarseCells.end()) return false;
            }
        }
    }
    return true;
}

void World::queueSpatial(Entity index) {
    if (!m_spatialActive) return;
    if (m_spatialQueued.size() <= index) m_spatialQueued.resize(m_alive.size(), 0);
    if (m_spatialQueued[index]) return;
    m_spatialQueued[index] = 1;
    m_spatialPending.push_back(index);
}

void World::markSpatialSubtree(Entity index) {
    if (!m_spatialActive) return;
    if (m_spatialRootQueued.size() <= index) m_spatialRootQueued.resize(m_alive.size(), 0);
    if (m_spatialRootQueued[index]) return;
    m_spatialRootQueued[index] = 1;
    m_spatialRoots.push_back(index);
}

void World::expandSpatialRoots() {
    auto roots = std::move(m_spatialRoots);
    m_spatialRoots.clear();
    for (Entity root : roots) {
        if (root < m_spatialRootQueued.size()) m_spatialRootQueued[root] = 0;
        if (root >= m_alive.size() || !m_alive[root]) continue;
        m_spatialStack.clear();
        m_spatialStack.push_back(root);
        while (!m_spatialStack.empty()) {
            const Entity current = m_spatialStack.back();
            m_spatialStack.pop_back();
            queueSpatial(current);
            for (Entity child = m_db.firstChild[current]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) m_spatialStack.push_back(child);
        }
    }
}

void World::removeSpatialEntry(Entity index) {
    SpatialEntry& entry = m_spatial[index];
    if (entry.state == StateCell) {
        const auto cellIt = m_cells.find(entry.cell);
        if (cellIt != m_cells.end()) {
            auto& bucket = cellIt->second;
            if (entry.slot < bucket.size() && bucket[entry.slot] == index) {
                const Entity moved = bucket.back();
                bucket[entry.slot] = moved;
                m_spatial[moved].slot = entry.slot;
                bucket.pop_back();
            }
            if (bucket.empty()) {
                noteCellRemoved(entry.cell);
                m_cells.erase(cellIt);
            }
        }
    } else if (entry.state == StateLarge) {
        m_largeEntities.erase(std::remove(m_largeEntities.begin(), m_largeEntities.end(), index), m_largeEntities.end());
    }
    entry.state = StateAbsent;
}

void World::insertSpatialEntry(Entity index) {
    SpatialEntry& entry = m_spatial[index];
    if (entry.state == StateLarge) {
        m_largeEntities.push_back(index);
    } else if (entry.state == StateCell) {
        m_maxSmallRadius = std::max(m_maxSmallRadius, entry.radius);
        growCellBounds(entry.cell);
        const auto [cellIt, created] = m_cells.try_emplace(entry.cell);
        if (created) noteCellAdded(entry.cell);
        auto& bucket = cellIt->second;
        entry.slot = static_cast<uint32_t>(bucket.size());
        bucket.push_back(index);
    }
}

glm::mat4 World::sweptWorld(Entity index, float& sweep) const {
    const auto baseLocal = [this](Entity entity) {
        glm::mat4 local = m_db.transforms[entity].localMatrix;
        if (isAnimated(entity)) local[3] = glm::vec4(m_animBase[entity - m_animOffset], 1.0f);
        return local;
    };
    glm::mat4 matrix = baseLocal(index);
    sweep = isAnimated(index) ? kOrbitReach : 0.0f;
    for (Entity ancestor = m_db.hierarchies[index].parent; ancestor != INVALID_ENTITY && m_alive[ancestor]; ancestor = m_db.hierarchies[ancestor].parent) {
        const glm::mat4 local = baseLocal(ancestor);
        if (sweep > 0.0f) sweep *= maxAxisScale(local);
        if (isAnimated(ancestor)) sweep += kOrbitReach;
        matrix = local * matrix;
    }
    return matrix;
}

void World::computeSpatialEntry(Entity index, SpatialEntry& out, bool swept) const {
    out.state = StateAbsent;
    out.animated = 0;
    if (!m_alive[index] || !m_visible[index] || !m_meshBounds || m_mesh[index] == NO_MESH) return;
    const glm::vec4 bounds = m_meshBounds(m_mesh[index]);
    float sweep = 0.0f;
    const glm::mat4 world = swept && !m_animBase.empty() ? sweptWorld(index, sweep) : worldNoCache(index);
    out.center = glm::vec3(world * glm::vec4(glm::vec3(bounds), 1.0f));
    out.radius = bounds.w * maxAxisScale(world) + sweep;
    out.animated = sweep > 0.0f ? 1 : 0;
    if (!std::isfinite(out.center.x + out.center.y + out.center.z) || std::isnan(out.radius)) return;
    const float cellLimit = static_cast<float>(kMaxKeyedCell) * m_cellSize;
    const bool beyondKey = std::abs(out.center.x) >= cellLimit || std::abs(out.center.y) >= cellLimit || std::abs(out.center.z) >= cellLimit;
    if (out.radius > m_cellSize * kLargeRadiusCells || beyondKey) {
        out.state = StateLarge;
        return;
    }
    out.cell = spatialKey(cellCoordinate(out.center.x, m_cellSize), cellCoordinate(out.center.y, m_cellSize), cellCoordinate(out.center.z, m_cellSize));
    out.state = StateCell;
}

SpatialEntry World::liveSpatial(Entity index) const {
    const SpatialEntry& indexed = m_spatial[index];
    if (!indexed.animated) return indexed;
    SpatialEntry live;
    computeSpatialEntry(index, live, false);
    return live;
}

const SpatialEntry& World::spatialView(Entity index, SpatialEntry& scratch) const {
    const SpatialEntry& indexed = m_spatial[index];
    if (!indexed.animated) return indexed;
    computeSpatialEntry(index, scratch, false);
    return scratch;
}

void World::buildSpatialIndex() {
    m_spatialActive = true;
    resetCellBounds();
    const size_t count = m_alive.size();
    m_spatial.assign(count, SpatialEntry{});
    m_spatialQueued.assign(count, 0);
    m_spatialPending.clear();
    m_spatialRoots.clear();
    m_spatialRootQueued.assign(count, 0);
    const auto compute = [this](size_t begin, size_t end) {
        for (size_t slot = begin; slot < end; ++slot) computeSpatialEntry(static_cast<Entity>(slot), m_spatial[slot], true);
    };
    if (m_jobs) {
        m_jobs->parallelFor(count, kParallelComputeGrain, compute);
    } else {
        compute(0, count);
    }
    for (Entity index = 0; index < count; ++index) insertSpatialEntry(index);
}

bool World::refreshSpatialInPlace(SpatialEntry& entry, const SpatialEntry& next) {
    if (entry.state != next.state || entry.state == StateLarge) return false;
    if (entry.state != StateAbsent && entry.cell != next.cell) return false;
    entry.center = next.center;
    entry.radius = next.radius;
    entry.animated = next.animated;
    if (entry.state == StateCell) m_maxSmallRadius = std::max(m_maxSmallRadius, entry.radius);
    return true;
}

void World::applySpatialUpdatesSerial(const std::vector<Entity>& pending, const std::vector<SpatialEntry>& fresh) {
    for (size_t position = 0; position < pending.size(); ++position) {
        const Entity index = pending[position];
        if (index >= m_alive.size()) continue;
        SpatialEntry& entry = m_spatial[index];
        if (refreshSpatialInPlace(entry, fresh[position])) continue;
        removeSpatialEntry(index);
        const uint32_t slot = entry.slot;
        entry = fresh[position];
        entry.slot = slot;
        insertSpatialEntry(index);
    }
}

void World::refreshSpatial() {
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::refreshSpatial");
    if (!m_spatialActive) {
        buildSpatialIndex();
        return;
    }
    expandSpatialRoots();
    auto pending = std::move(m_spatialPending);
    m_spatialPending.clear();
    if (m_spatial.size() < m_alive.size()) m_spatial.resize(m_alive.size());
    for (Entity index : pending) {
        if (index < m_spatialQueued.size()) m_spatialQueued[index] = 0;
    }
    std::vector<SpatialEntry> fresh(pending.size());
    const auto compute = [&](size_t begin, size_t end) {
        for (size_t position = begin; position < end; ++position) {
            if (pending[position] < m_alive.size()) computeSpatialEntry(pending[position], fresh[position], true);
        }
    };
    if (m_jobs && pending.size() > kParallelComputeThreshold) {
        m_jobs->parallelFor(pending.size(), kParallelComputeGrain, compute);
    } else {
        compute(0, pending.size());
    }
    if (!m_jobs || pending.size() < kParallelApplyThreshold) {
        applySpatialUpdatesSerial(pending, fresh);
    } else {
        applySpatialUpdatesParallel(pending, fresh);
    }
}
