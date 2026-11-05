module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDegreesToRadians = kPi / 180.0f;
constexpr float kRegionHeightExtent = 1.0e9f;
constexpr float kDegenerateNormalLength = 1.0e-12f;
constexpr float kFloorThickness = 0.01f;
constexpr float kFloorMergeDistance = 0.15f;
constexpr float kProjectionInfinity = 1.0e30f;
constexpr float kCellLimit = 500000.0f;

bool triangleOverlapsRect(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float minX, float minZ, float maxX, float maxZ) {
    const glm::vec2 corners[3] = {{a.x, a.z}, {b.x, b.z}, {c.x, c.z}};
    for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
        const glm::vec2 edge = corners[(edgeIndex + 1) % 3] - corners[edgeIndex];
        const glm::vec2 axis(-edge.y, edge.x);
        float triangleMin = kProjectionInfinity;
        float triangleMax = -kProjectionInfinity;
        for (const glm::vec2& corner : corners) {
            const float projection = axis.x * corner.x + axis.y * corner.y;
            triangleMin = std::min(triangleMin, projection);
            triangleMax = std::max(triangleMax, projection);
        }
        float rectMin = kProjectionInfinity;
        float rectMax = -kProjectionInfinity;
        for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
            const float projection = axis.x * ((cornerIndex & 1) ? maxX : minX) + axis.y * ((cornerIndex & 2) ? maxZ : minZ);
            rectMin = std::min(rectMin, projection);
            rectMax = std::max(rectMax, projection);
        }
        if (triangleMax < rectMin || rectMax < triangleMin) return false;
    }
    return true;
}

struct Column {
    std::vector<float> floors;
    std::vector<std::pair<float, float>> blockers;
};

struct TileScratch {
    std::vector<Column> columns;
    std::vector<std::vector<float>> spans;
    std::vector<uint32_t> triangles;
};

TileScratch& tileScratch() {
    thread_local TileScratch scratch;
    return scratch;
}

struct TileRaster {
    const NavConfig& config;
    int tileCells;
    int radius;
    int pad;
    int width;
    int regionCellX;
    int regionCellZ;
    Aabb region;
    std::vector<Column>& columns;
    std::vector<std::vector<float>>& spanStorage;

    TileRaster(const NavConfig& navConfig, int tileX, int tileZ, TileScratch& scratch)
        : config(navConfig), tileCells(navConfig.tileCells), radius(static_cast<int>(std::ceil(navConfig.agentRadius / navConfig.cellSize))), pad(radius + 1), width(tileCells + 2 * pad),
          regionCellX(tileX * tileCells - pad), regionCellZ(tileZ * tileCells - pad), columns(scratch.columns), spanStorage(scratch.spans) {
        const size_t columnCount = static_cast<size_t>(width) * static_cast<size_t>(width);
        if (columns.size() < columnCount) columns.resize(columnCount);
        for (size_t index = 0; index < columnCount; ++index) {
            columns[index].floors.clear();
            columns[index].blockers.clear();
        }
        const float cellSize = config.cellSize;
        region.lo = glm::vec3(config.originX + regionCellX * cellSize, -kRegionHeightExtent, config.originZ + regionCellZ * cellSize);
        region.hi = glm::vec3(config.originX + (regionCellX + width) * cellSize, kRegionHeightExtent, config.originZ + (regionCellZ + width) * cellSize);
    }

    int cellOf(float value, float origin) const {
        const float cell = std::floor((value - origin) / config.cellSize);
        if (std::isnan(cell)) return static_cast<int>(kCellLimit);
        return static_cast<int>(std::clamp(cell, -kCellLimit, kCellLimit));
    }

    Column& column(int cellX, int cellZ) { return columns[static_cast<size_t>(cellZ - regionCellZ) * static_cast<size_t>(width) + static_cast<size_t>(cellX - regionCellX)]; }

    void addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float cosSlope) {
        glm::vec3 normal = glm::cross(b - a, c - a);
        const float length = glm::length(normal);
        if (length < kDegenerateNormalLength) return;
        normal = normal / length;
        const bool isFloor = normal.y >= cosSlope;
        const float lowY = std::min({a.y, b.y, c.y});
        const float highY = std::max({a.y, b.y, c.y});
        const int firstX = std::max(regionCellX, cellOf(std::min({a.x, b.x, c.x}), config.originX));
        const int lastX = std::min(regionCellX + width - 1, cellOf(std::max({a.x, b.x, c.x}), config.originX));
        const int firstZ = std::max(regionCellZ, cellOf(std::min({a.z, b.z, c.z}), config.originZ));
        const int lastZ = std::min(regionCellZ + width - 1, cellOf(std::max({a.z, b.z, c.z}), config.originZ));
        const float cellSize = config.cellSize;
        for (int cellZ = firstZ; cellZ <= lastZ; ++cellZ) {
            for (int cellX = firstX; cellX <= lastX; ++cellX) {
                const float minX = config.originX + cellX * cellSize;
                const float minZ = config.originZ + cellZ * cellSize;
                if (!triangleOverlapsRect(a, b, c, minX, minZ, minX + cellSize, minZ + cellSize)) continue;
                Column& target = column(cellX, cellZ);
                if (!isFloor) {
                    target.blockers.emplace_back(lowY, highY);
                    continue;
                }
                const float centerX = minX + cellSize * 0.5f;
                const float centerZ = minZ + cellSize * 0.5f;
                const float height = std::clamp(a.y - (normal.x * (centerX - a.x) + normal.z * (centerZ - a.z)) / normal.y, lowY, highY);
                target.floors.push_back(height);
                target.blockers.emplace_back(height - kFloorThickness, height + kFloorThickness);
            }
        }
    }

    void addObstacle(const Aabb& box) {
        if (!box.overlaps(region)) return;
        const int firstX = std::max(regionCellX, cellOf(box.lo.x, config.originX));
        const int lastX = std::min(regionCellX + width - 1, cellOf(box.hi.x, config.originX));
        const int firstZ = std::max(regionCellZ, cellOf(box.lo.z, config.originZ));
        const int lastZ = std::min(regionCellZ + width - 1, cellOf(box.hi.z, config.originZ));
        for (int cellZ = firstZ; cellZ <= lastZ; ++cellZ) {
            for (int cellX = firstX; cellX <= lastX; ++cellX) column(cellX, cellZ).blockers.emplace_back(box.lo.y, box.hi.y);
        }
    }

    std::vector<std::vector<float>>& freeSpans() {
        const size_t columnCount = static_cast<size_t>(width) * static_cast<size_t>(width);
        std::vector<std::vector<float>>& spans = spanStorage;
        if (spans.size() < columnCount) spans.resize(columnCount);
        for (size_t index = 0; index < columnCount; ++index) {
            spans[index].clear();
            std::vector<float>& floors = columns[index].floors;
            std::sort(floors.begin(), floors.end());
            float lastKept = 0.0f;
            bool hasKept = false;
            for (const float floorHeight : floors) {
                if (hasKept && floorHeight - lastKept < kFloorMergeDistance) continue;
                lastKept = floorHeight;
                hasKept = true;
                if (!isBlocked(columns[index], floorHeight)) spans[index].push_back(floorHeight);
            }
        }
        return spans;
    }

    bool isBlocked(const Column& target, float floorHeight) const {
        return std::any_of(target.blockers.begin(), target.blockers.end(),
                           [&](const std::pair<float, float>& blocker) { return blocker.first < floorHeight + config.agentHeight && blocker.second > floorHeight + config.maxClimb; });
    }

    bool hasClearance(const std::vector<std::vector<float>>& spans, int paddedX, int paddedZ, float height) const {
        for (int offsetZ = -radius; offsetZ <= radius; ++offsetZ) {
            for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
                if (offsetX * offsetX + offsetZ * offsetZ > radius * radius) continue;
                const std::vector<float>& neighbor = spans[static_cast<size_t>(paddedZ + offsetZ) * static_cast<size_t>(width) + static_cast<size_t>(paddedX + offsetX)];
                const bool reachable = std::any_of(neighbor.begin(), neighbor.end(), [&](float other) { return std::fabs(other - height) <= config.maxClimb; });
                if (!reachable) return false;
            }
        }
        return true;
    }

    void collect(const std::vector<std::vector<float>>& spans, std::vector<uint32_t>& start, std::vector<float>& heights) const {
        start.assign(static_cast<size_t>(tileCells) * static_cast<size_t>(tileCells) + 1, 0);
        for (int localZ = 0; localZ < tileCells; ++localZ) {
            for (int localX = 0; localX < tileCells; ++localX) {
                start[static_cast<size_t>(localZ) * static_cast<size_t>(tileCells) + static_cast<size_t>(localX)] = static_cast<uint32_t>(heights.size());
                const int paddedX = localX + pad;
                const int paddedZ = localZ + pad;
                for (const float height : spans[static_cast<size_t>(paddedZ) * static_cast<size_t>(width) + static_cast<size_t>(paddedX)]) {
                    if (hasClearance(spans, paddedX, paddedZ, height)) heights.push_back(height);
                }
            }
        }
        start[static_cast<size_t>(tileCells) * static_cast<size_t>(tileCells)] = static_cast<uint32_t>(heights.size());
    }
};
}

void NavMesh::buildTile(int tx, int tz) {
    TileScratch& scratch = tileScratch();
    TileRaster raster(m_config, tx, tz, scratch);
    const float cosSlope = std::cos(m_config.maxSlopeDegrees * kDegreesToRadians);

    std::vector<uint32_t>& triangles = scratch.triangles;
    triangles.clear();
    m_bvh.query(raster.region, triangles);
    const TriangleSoup& soup = m_bvh.soup();
    for (const uint32_t triangle : triangles) {
        const glm::vec3& a = soup.vertices[soup.indices[triangle * 3]];
        const glm::vec3& b = soup.vertices[soup.indices[triangle * 3 + 1]];
        const glm::vec3& c = soup.vertices[soup.indices[triangle * 3 + 2]];
        raster.addTriangle(a, b, c, cosSlope);
    }
    for (const auto& [id, obstacle] : m_obstacles) raster.addObstacle(obstacle.box);

    Tile tile;
    tile.builtVersion = m_version + 1;
    raster.collect(raster.freeSpans(), tile.start, tile.heights);
    m_tiles[tileKey(tx, tz)] = std::move(tile);
}
