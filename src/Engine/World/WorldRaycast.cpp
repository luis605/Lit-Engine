module;

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr float kDirectionEpsilon = 1.0e-8f;
constexpr float kSlabEpsilon = 1.0e-3f;
constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr size_t kStepSlack = 8;
constexpr int kAxisCount = 3;
constexpr int kYShift = 21;
constexpr int kXShift = 42;

struct GridWalker {
    int cell[kAxisCount];
    int step[kAxisCount];
    float nextBoundary[kAxisCount];
    float boundaryDelta[kAxisCount];

    GridWalker(const glm::vec3& origin, const glm::vec3& direction, float cellSize) {
        for (int axis = 0; axis < kAxisCount; ++axis) {
            cell[axis] = cellCoordinate(origin[axis], cellSize);
            if (direction[axis] > 0.0f) {
                step[axis] = 1;
                nextBoundary[axis] = (static_cast<float>(cell[axis] + 1) * cellSize - origin[axis]) / direction[axis];
                boundaryDelta[axis] = cellSize / direction[axis];
            } else if (direction[axis] < 0.0f) {
                step[axis] = -1;
                nextBoundary[axis] = (static_cast<float>(cell[axis]) * cellSize - origin[axis]) / direction[axis];
                boundaryDelta[axis] = -cellSize / direction[axis];
            } else {
                step[axis] = 0;
                nextBoundary[axis] = kInfinity;
                boundaryDelta[axis] = kInfinity;
            }
        }
    }

    int advance(float& distance) {
        const int axis = (nextBoundary[0] < nextBoundary[1]) ? (nextBoundary[0] < nextBoundary[2] ? 0 : 2) : (nextBoundary[1] < nextBoundary[2] ? 1 : 2);
        if (std::isinf(nextBoundary[axis])) return -1;
        distance = nextBoundary[axis];
        cell[axis] += step[axis];
        nextBoundary[axis] += boundaryDelta[axis];
        return axis;
    }
};

struct RayBoxTest {
    float origin[kAxisCount];
    float inverse[kAxisCount];
    bool parallel[kAxisCount];
    float cellSize;
    float margin;

    RayBoxTest(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float cellExtent, float boxMargin)
        : cellSize(cellExtent), margin(boxMargin) {
        for (int axis = 0; axis < kAxisCount; ++axis) {
            origin[axis] = rayOrigin[axis];
            parallel[axis] = rayDirection[axis] == 0.0f;
            inverse[axis] = 1.0f / rayDirection[axis];
        }
    }

    bool hits(int cellX, int cellY, int cellZ, float maxDistance) const {
        const int cell[kAxisCount] = {cellX, cellY, cellZ};
        float near = 0.0f;
        float far = maxDistance;
        for (int axis = 0; axis < kAxisCount; ++axis) {
            const float low = static_cast<float>(cell[axis]) * cellSize - margin;
            const float high = low + cellSize + 2.0f * margin;
            if (parallel[axis]) {
                if (origin[axis] < low || origin[axis] > high) return false;
                continue;
            }
            const float first = (low - origin[axis]) * inverse[axis];
            const float second = (high - origin[axis]) * inverse[axis];
            near = std::max(near, std::min(first, second));
            far = std::min(far, std::max(first, second));
            if (near > far) return false;
        }
        return true;
    }
};

}

std::optional<RayHit> World::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    refreshSpatial();
    const float length = glm::length(direction);
    if (!(length >= kDirectionEpsilon) || !std::isfinite(length) || !std::isfinite(origin.x + origin.y + origin.z) || std::isnan(maxDistance)) return std::nullopt;
    const glm::vec3 unitDirection = direction / length;

    const double exactLength = std::sqrt(static_cast<double>(direction.x) * direction.x + static_cast<double>(direction.y) * direction.y + static_cast<double>(direction.z) * direction.z);
    const double exactDirection[kAxisCount] = {direction.x / exactLength, direction.y / exactLength, direction.z / exactLength};

    std::optional<RayHit> best;
    const auto test = [&](Entity entity) {
        SpatialEntry scratch;
        const SpatialEntry& entry = spatialView(entity, scratch);
        const double toCenterX = static_cast<double>(entry.center.x) - origin.x;
        const double toCenterY = static_cast<double>(entry.center.y) - origin.y;
        const double toCenterZ = static_cast<double>(entry.center.z) - origin.z;
        const double along = toCenterX * exactDirection[0] + toCenterY * exactDirection[1] + toCenterZ * exactDirection[2];
        const double perpendicular2 = toCenterX * toCenterX + toCenterY * toCenterY + toCenterZ * toCenterZ - along * along;
        const double radius2 = static_cast<double>(entry.radius) * entry.radius;
        if (perpendicular2 > radius2) return;
        const double half = std::sqrt(radius2 - perpendicular2);
        double exactDistance = along - half;
        if (exactDistance < 0.0) exactDistance = along + half;
        const float distance = static_cast<float>(exactDistance);
        if (distance < 0.0f || distance > maxDistance) return;
        if (!best || distance < best->distance || (distance == best->distance && entity < best->entity.index)) best = RayHit{{entity, m_generation[entity]}, distance};
    };
    for (Entity entity : m_largeEntities) test(entity);
    if (m_cells.empty()) return best;

    const int inflate = static_cast<int>(std::ceil(m_maxSmallRadius / m_cellSize));
    const float margin = m_maxSmallRadius + m_cellSize;
    const RayBoxTest boxTest(origin, unitDirection, m_cellSize, m_maxSmallRadius + kSlabEpsilon);
    const auto visit = [&](int cellX, int cellY, int cellZ) {
        const float reachLimit = best ? std::min(maxDistance, best->distance) : maxDistance;
        if (!boxTest.hits(cellX, cellY, cellZ, reachLimit + kSlabEpsilon)) return;
        const std::vector<Entity>* bucket = cellBucket(cellX, cellY, cellZ);
        if (!bucket) return;
        for (Entity entity : *bucket) test(entity);
    };
    const auto visitLayer = [&](const GridWalker& walker, int layerAxis, int layerOffset) {
        const int axisB = (layerAxis + 1) % kAxisCount;
        const int axisC = (layerAxis + 2) % kAxisCount;
        int cell[kAxisCount] = {walker.cell[0], walker.cell[1], walker.cell[2]};
        cell[layerAxis] += layerOffset;
        const int baseB = cell[axisB];
        const int baseC = cell[axisC];
        for (int offsetB = -inflate; offsetB <= inflate; ++offsetB) {
            for (int offsetC = -inflate; offsetC <= inflate; ++offsetC) {
                cell[axisB] = baseB + offsetB;
                cell[axisC] = baseC + offsetC;
                visit(cell[0], cell[1], cell[2]);
            }
        }
    };

    float entryDistance = 0.0f;
    float exitDistance = maxDistance;
    const float clipMargin = m_maxSmallRadius + m_cellSize + kSlabEpsilon;
    for (int axis = 0; axis < kAxisCount; ++axis) {
        const float low = static_cast<float>(m_cellBounds[axis]) * m_cellSize - clipMargin;
        const float high = static_cast<float>(m_cellBounds[axis + kAxisCount] + 1) * m_cellSize + clipMargin;
        if (unitDirection[axis] == 0.0f) {
            if (origin[axis] < low || origin[axis] > high) return best;
            continue;
        }
        const float first = (low - origin[axis]) / unitDirection[axis];
        const float second = (high - origin[axis]) / unitDirection[axis];
        entryDistance = std::max(entryDistance, std::min(first, second));
        exitDistance = std::min(exitDistance, std::max(first, second));
    }
    if (entryDistance > exitDistance) return best;

    const double walkLength = static_cast<double>(exitDistance) - static_cast<double>(entryDistance);
    const double axisSum = std::abs(static_cast<double>(unitDirection.x)) + std::abs(static_cast<double>(unitDirection.y)) + std::abs(static_cast<double>(unitDirection.z));
    const double stepBound = axisSum * walkLength / static_cast<double>(m_cellSize) + kStepSlack;
    const double layerCells = static_cast<double>(2 * inflate + 1) * static_cast<double>(2 * inflate + 1);
    const double boundsCells = static_cast<double>(m_cellBounds[3] - m_cellBounds[0] + 1) * static_cast<double>(m_cellBounds[4] - m_cellBounds[1] + 1) * static_cast<double>(m_cellBounds[5] - m_cellBounds[2] + 1);
    const double fill = std::min(1.0, static_cast<double>(m_cells.size()) / boundsCells);
    const bool skipEmptyBlocks = inflate <= kCoarseSpan;
    const double walkCost = stepBound * (skipEmptyBlocks ? 1.0 + layerCells * fill : layerCells);
    bool exhausted = true;
    if (walkCost < static_cast<double>(m_cells.size())) {
        const float startDistance = std::max(0.0f, entryDistance - m_cellSize);
        GridWalker walker(origin + unitDirection * startDistance, unitDirection, m_cellSize);
        float cellDistance = startDistance;
        int advancedAxis = -1;
        int blockCoarse[kAxisCount] = {0, 0, 0};
        bool blockKnown = false;
        bool blockEmpty = false;
        const size_t stepLimit = static_cast<size_t>(stepBound) + kStepSlack;
        for (size_t iteration = 0; iteration < stepLimit; ++iteration) {
            if (cellDistance > maxDistance + margin || (best && cellDistance > best->distance + margin)) {
                exhausted = false;
                break;
            }
            if (skipEmptyBlocks) {
                const int coarse[kAxisCount] = {walker.cell[0] >> kCoarseShift, walker.cell[1] >> kCoarseShift, walker.cell[2] >> kCoarseShift};
                if (!blockKnown || coarse[0] != blockCoarse[0] || coarse[1] != blockCoarse[1] || coarse[2] != blockCoarse[2]) {
                    blockKnown = true;
                    for (int axis = 0; axis < kAxisCount; ++axis) blockCoarse[axis] = coarse[axis];
                    blockEmpty = coarseBlockEmpty(coarse[0], coarse[1], coarse[2]);
                }
            }
            if (!blockEmpty && advancedAxis < 0) {
                for (int offsetX = -inflate; offsetX <= inflate; ++offsetX) {
                    for (int offsetY = -inflate; offsetY <= inflate; ++offsetY) {
                        for (int offsetZ = -inflate; offsetZ <= inflate; ++offsetZ) visit(walker.cell[0] + offsetX, walker.cell[1] + offsetY, walker.cell[2] + offsetZ);
                    }
                }
            } else if (!blockEmpty) {
                visitLayer(walker, advancedAxis, walker.step[advancedAxis] * inflate);
            }
            float stepDistance = 0.0f;
            advancedAxis = walker.advance(stepDistance);
            cellDistance = startDistance + stepDistance;
            if (advancedAxis < 0) {
                exhausted = false;
                break;
            }
        }
    }
    if (exhausted) {
        for (const auto& [key, bucket] : m_cells) {
            const float reachLimit = best ? std::min(maxDistance, best->distance) : maxDistance;
            if (!boxTest.hits(decodeCellCoordinate(key, kXShift), decodeCellCoordinate(key, kYShift), decodeCellCoordinate(key, 0), reachLimit + kSlabEpsilon)) continue;
            for (Entity entity : bucket) test(entity);
        }
    }
    return best;
}
