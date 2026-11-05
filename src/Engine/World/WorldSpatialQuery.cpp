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
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr int kFrustumCornerCount = 8;
constexpr int kFrustumPlaneCount = 6;
constexpr int kYShift = 21;
constexpr int kXShift = 42;
constexpr float kCullEpsilon = 1.0e-3f;

enum CellClass : int { CellSkip = 0, CellPartial = 1, CellFull = 2 };

std::array<glm::vec4, kFrustumPlaneCount> extractFrustumPlanes(const glm::mat4& viewProjection) {
    const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
    const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
    const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]);
    const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);
    std::array<glm::vec4, kFrustumPlaneCount> planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};
    for (glm::vec4& plane : planes) plane /= glm::length(glm::vec3(plane));
    return planes;
}

void frustumCornerBounds(const glm::mat4& viewProjection, glm::vec3& low, glm::vec3& high) {
    const glm::mat4 inverse = glm::inverse(viewProjection);
    low = glm::vec3(std::numeric_limits<float>::max());
    high = glm::vec3(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < kFrustumCornerCount; ++corner) {
        const glm::vec4 clip((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? 1.0f : 0.0f, 1.0f);
        const glm::vec4 transformed = inverse * clip;
        const glm::vec3 point = glm::vec3(transformed) / transformed.w;
        low = glm::min(low, point);
        high = glm::max(high, point);
    }
}

constexpr size_t kBitsPerWord = 64;
constexpr size_t kBitmapSortDivisor = 16;

template <typename Generations>
void sortByIndex(std::vector<EntityHandle>& handles, const Generations& generations) {
    const size_t universe = generations.size();
    if (handles.size() * kBitmapSortDivisor < universe) {
        std::sort(handles.begin(), handles.end(), [](EntityHandle lhs, EntityHandle rhs) { return lhs.index < rhs.index; });
        return;
    }
    thread_local std::vector<uint64_t> bits;
    bits.assign((universe + kBitsPerWord - 1) / kBitsPerWord, 0);
    for (EntityHandle handle : handles) bits[handle.index / kBitsPerWord] |= uint64_t{1} << (handle.index % kBitsPerWord);
    size_t write = 0;
    for (size_t word = 0; word < bits.size(); ++word) {
        for (uint64_t remaining = bits[word]; remaining != 0; remaining &= remaining - 1) {
            const Entity index = static_cast<Entity>(word * kBitsPerWord + static_cast<size_t>(__builtin_ctzll(remaining)));
            handles[write++] = {index, generations[index]};
        }
    }
}

int classifyBoxAgainstPlanes(const std::array<glm::vec4, kFrustumPlaneCount>& planes, const glm::vec3& low, const glm::vec3& high) {
    int result = CellFull;
    for (const glm::vec4& plane : planes) {
        const glm::vec3 normal(plane);
        const glm::vec3 bestCorner(normal.x > 0.0f ? high.x : low.x, normal.y > 0.0f ? high.y : low.y, normal.z > 0.0f ? high.z : low.z);
        if (glm::dot(normal, bestCorner) + plane.w < -kCullEpsilon) return CellSkip;
        const glm::vec3 worstCorner(normal.x > 0.0f ? low.x : high.x, normal.y > 0.0f ? low.y : high.y, normal.z > 0.0f ? low.z : high.z);
        if (glm::dot(normal, worstCorner) + plane.w < kCullEpsilon) result = CellPartial;
    }
    return result;
}

int classifyBoxAgainstSphere(const glm::vec3& center, float radius, float margin, const glm::vec3& low, const glm::vec3& high) {
    const glm::vec3 nearest = glm::clamp(center, low, high);
    const glm::vec3 gap = nearest - center;
    const float reach = radius + margin;
    if (glm::dot(gap, gap) > reach * reach) return CellSkip;
    const glm::vec3 farthest = glm::max(glm::abs(center - low), glm::abs(center - high));
    const float inner = radius - kCullEpsilon;
    return inner > 0.0f && glm::dot(farthest, farthest) <= inner * inner ? CellFull : CellPartial;
}

}

const std::vector<Entity>* World::cellBucket(int cellX, int cellY, int cellZ) const {
    const auto cellIt = m_cells.find(spatialKey(cellX, cellY, cellZ));
    return cellIt == m_cells.end() ? nullptr : &cellIt->second;
}

template <typename Filter, typename Visitor>
void World::visitCells(const std::array<int, 6>& bounds, Filter&& filter, Visitor&& visitor) const {
    const double span = static_cast<double>(bounds[3] - bounds[0] + 1) * static_cast<double>(bounds[4] - bounds[1] + 1) * static_cast<double>(bounds[5] - bounds[2] + 1);
    if (span > static_cast<double>(m_cells.size())) {
        for (const auto& [key, bucket] : m_cells) {
            const int cellX = decodeCellCoordinate(key, kXShift);
            const int cellY = decodeCellCoordinate(key, kYShift);
            const int cellZ = decodeCellCoordinate(key, 0);
            if (cellX < bounds[0] || cellX > bounds[3] || cellY < bounds[1] || cellY > bounds[4] || cellZ < bounds[2] || cellZ > bounds[5]) continue;
            const int cellClass = filter(cellX, cellY, cellZ);
            if (cellClass == CellSkip) continue;
            for (Entity entity : bucket) visitor(entity, cellClass == CellFull);
        }
        return;
    }
    for (int cellX = bounds[0]; cellX <= bounds[3]; ++cellX) {
        for (int cellY = bounds[1]; cellY <= bounds[4]; ++cellY) {
            for (int cellZ = bounds[2]; cellZ <= bounds[5]; ++cellZ) {
                const int cellClass = filter(cellX, cellY, cellZ);
                if (cellClass == CellSkip) continue;
                const std::vector<Entity>* bucket = cellBucket(cellX, cellY, cellZ);
                if (!bucket) continue;
                for (Entity entity : *bucket) visitor(entity, cellClass == CellFull);
            }
        }
    }
}

std::vector<EntityHandle> World::overlapSphere(const glm::vec3& center, float radius) {
    std::vector<EntityHandle> overlapping;
    overlapSphere(center, radius, overlapping);
    return overlapping;
}

void World::overlapSphere(const glm::vec3& center, float radius, std::vector<EntityHandle>& out) {
    refreshSpatial();
    out.clear();
    if (std::isnan(radius) || !std::isfinite(center.x + center.y + center.z)) return;
    const bool exact = m_animBase.empty();
    const auto test = [&](Entity entity, bool full) {
        if (full && exact) {
            out.push_back({entity, m_generation[entity]});
            return;
        }
        SpatialEntry scratch;
        const SpatialEntry& entry = spatialView(entity, scratch);
        const float reach = radius + entry.radius;
        const glm::vec3 offset = entry.center - center;
        if (glm::dot(offset, offset) <= reach * reach) out.push_back({entity, m_generation[entity]});
    };

    const float reach = radius + m_maxSmallRadius;
    const std::array<int, 6> bounds = {cellCoordinate(center.x - reach, m_cellSize), cellCoordinate(center.y - reach, m_cellSize), cellCoordinate(center.z - reach, m_cellSize), cellCoordinate(center.x + reach, m_cellSize), cellCoordinate(center.y + reach, m_cellSize), cellCoordinate(center.z + reach, m_cellSize)};
    const auto filter = [&](int cellX, int cellY, int cellZ) {
        const glm::vec3 low(static_cast<float>(cellX) * m_cellSize, static_cast<float>(cellY) * m_cellSize, static_cast<float>(cellZ) * m_cellSize);
        return classifyBoxAgainstSphere(center, radius, m_maxSmallRadius + kCullEpsilon, low, low + glm::vec3(m_cellSize));
    };
    visitCells(bounds, filter, test);
    for (Entity entity : m_largeEntities) test(entity, false);
    sortByIndex(out, m_generation);
}

std::vector<EntityHandle> World::queryFrustum(const Camera& camera) {
    std::vector<EntityHandle> visibleHandles;
    queryFrustum(camera, visibleHandles);
    return visibleHandles;
}

void World::queryFrustum(const Camera& camera, std::vector<EntityHandle>& out) {
    refreshSpatial();
    out.clear();
    const glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
    const std::array<glm::vec4, kFrustumPlaneCount> planes = extractFrustumPlanes(viewProjection);
    const bool exact = m_animBase.empty();

    const auto collect = [&](Entity entity, bool full) {
        if (full && exact) {
            out.push_back({entity, m_generation[entity]});
            return;
        }
        SpatialEntry scratch;
        const SpatialEntry& entry = spatialView(entity, scratch);
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), entry.center) + plane.w < -entry.radius) return;
        }
        out.push_back({entity, m_generation[entity]});
    };

    glm::vec3 low, high;
    frustumCornerBounds(viewProjection, low, high);
    low -= glm::vec3(m_maxSmallRadius);
    high += glm::vec3(m_maxSmallRadius);
    const std::array<int, 6> bounds = {cellCoordinate(low.x, m_cellSize), cellCoordinate(low.y, m_cellSize), cellCoordinate(low.z, m_cellSize), cellCoordinate(high.x, m_cellSize), cellCoordinate(high.y, m_cellSize), cellCoordinate(high.z, m_cellSize)};
    const glm::vec3 margin(m_maxSmallRadius + kCullEpsilon);
    const auto filter = [&](int cellX, int cellY, int cellZ) {
        const glm::vec3 cellLow(static_cast<float>(cellX) * m_cellSize, static_cast<float>(cellY) * m_cellSize, static_cast<float>(cellZ) * m_cellSize);
        return classifyBoxAgainstPlanes(planes, cellLow - margin, cellLow + glm::vec3(m_cellSize) + margin);
    };
    visitCells(bounds, filter, collect);
    for (Entity entity : m_largeEntities) collect(entity, false);
    sortByIndex(out, m_generation);
}
