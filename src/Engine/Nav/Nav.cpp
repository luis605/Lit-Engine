module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr int kKeyBias = 524288;
constexpr float kColliderGeometryLimit = 60.0f;
constexpr int kTileKeyShift = 32;
constexpr int kNodeXShift = 36;
constexpr int kNodeZShift = 8;
constexpr uint64_t kLayerMask = 0xFFu;
constexpr uint64_t kNodeZMask = 0xFFFFFFFull;
constexpr float kDirtyPaddingCells = 2.0f;
constexpr float kWalkableQueryCells = 0.75f;
constexpr float kLineStartSnapCells = 1.5f;
constexpr float kLineStepCells = 0.5f;
constexpr float kLineClimbFactor = 0.6f;
constexpr float kConnectionTolerance = 1.0e-4f;
constexpr float kHashHeightScale = 1000.0f;
constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr float kCellLimit = 500000.0f;
constexpr float kMinCellSize = 0.001f;
constexpr int kMaxTileCells = 1024;
constexpr float kMaxRadiusCells = 64.0f;
constexpr float kMaxSlopeLimit = 89.0f;
constexpr int kMaxLineSteps = 65536;
constexpr int kBoundsCellMargin = 1;
constexpr int kMaxTilesPerAxis = 1024;

int floorDiv(int dividend, int divisor) {
    int quotient = dividend / divisor;
    if ((dividend % divisor != 0) && ((dividend < 0) != (divisor < 0))) --quotient;
    return quotient;
}

float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

NavConfig sanitized(NavConfig config) {
    const NavConfig defaults;
    config.cellSize = std::isfinite(config.cellSize) && config.cellSize >= kMinCellSize ? config.cellSize : defaults.cellSize;
    config.agentRadius = std::clamp(finiteOr(config.agentRadius, defaults.agentRadius), 0.0f, config.cellSize * kMaxRadiusCells);
    config.agentHeight = std::max(finiteOr(config.agentHeight, defaults.agentHeight), 0.0f);
    config.maxClimb = std::max(finiteOr(config.maxClimb, defaults.maxClimb), 0.0f);
    config.maxSlopeDegrees = std::clamp(finiteOr(config.maxSlopeDegrees, defaults.maxSlopeDegrees), 0.0f, kMaxSlopeLimit);
    config.tileCells = std::clamp(config.tileCells, 1, kMaxTileCells);
    config.originX = finiteOr(config.originX, 0.0f);
    config.originZ = finiteOr(config.originZ, 0.0f);
    config.snapDistance = std::max(finiteOr(config.snapDistance, defaults.snapDistance), 0.0f);
    return config;
}

float squaredDistance(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 delta = a - b;
    return delta.x * delta.x + delta.z * delta.z + delta.y * delta.y;
}
}

uint64_t NavMesh::tileKey(int tx, int tz) { return (static_cast<uint64_t>(tx + kKeyBias) << kTileKeyShift) | static_cast<uint64_t>(tz + kKeyBias); }

uint64_t NavMesh::nodeKey(const NavNodeId& node) {
    return (static_cast<uint64_t>(node.cx + kKeyBias) << kNodeXShift) | (static_cast<uint64_t>(node.cz + kKeyBias) << kNodeZShift) | (node.layer & kLayerMask);
}

NavNodeId NavMesh::nodeFromKey(uint64_t key) {
    return {static_cast<int>(key >> kNodeXShift) - kKeyBias, static_cast<int>((key >> kNodeZShift) & kNodeZMask) - kKeyBias, static_cast<uint32_t>(key & kLayerMask)};
}

int NavMesh::cellOf(float value, float origin) const {
    const float cell = std::floor((value - origin) / m_config.cellSize);
    if (std::isnan(cell)) return static_cast<int>(kCellLimit);
    return static_cast<int>(std::clamp(cell, -kCellLimit, kCellLimit));
}

int NavMesh::tileOf(int cell) const { return floorDiv(cell, m_config.tileCells); }

void NavMesh::configure(const NavConfig& config) {
    m_config = sanitized(config);
    m_tiles.clear();
    m_dirty.clear();
    ++m_version;
    m_resetVersion = m_version;
    if (m_bvh.triangleCount() > 0) setGeometry(m_bvh.soup());
}

void NavMesh::setGeometry(const TriangleSoup& soup) {
    m_bvh.build(soup);
    m_bounds = m_bvh.bounds();
    m_tiles.clear();
    m_dirty.clear();
    ++m_version;
    m_resetVersion = m_version;
    if (!m_bounds.valid()) return;
    markDirty(m_bounds);
}

void NavMesh::buildFromWorld(World& world, uint32_t layerMask) {
    TriangleSoup soup;
    collectColliderGeometry(world, soup, layerMask, kColliderGeometryLimit);
    setGeometry(soup);
    rebuildAll();
}

void NavMesh::markDirty(const Aabb& box) {
    if (!m_bounds.valid() || std::isnan(box.lo.x) || std::isnan(box.hi.x) || std::isnan(box.lo.z) || std::isnan(box.hi.z)) return;
    const float padding = (std::ceil(m_config.agentRadius / m_config.cellSize) + kDirtyPaddingCells) * m_config.cellSize;
    const float lowX = std::max(box.lo.x, m_bounds.lo.x - padding);
    const float highX = std::min(box.hi.x, m_bounds.hi.x + padding);
    const float lowZ = std::max(box.lo.z, m_bounds.lo.z - padding);
    const float highZ = std::min(box.hi.z, m_bounds.hi.z + padding);
    if (lowX > highX || lowZ > highZ) return;
    const int firstX = tileOf(cellOf(lowX - padding, m_config.originX));
    const int lastX = std::min(tileOf(cellOf(highX + padding, m_config.originX)), firstX + kMaxTilesPerAxis - 1);
    const int firstZ = tileOf(cellOf(lowZ - padding, m_config.originZ));
    const int lastZ = std::min(tileOf(cellOf(highZ + padding, m_config.originZ)), firstZ + kMaxTilesPerAxis - 1);
    for (int tileX = firstX; tileX <= lastX; ++tileX) {
        for (int tileZ = firstZ; tileZ <= lastZ; ++tileZ) m_dirty.insert({tileX, tileZ});
    }
}

uint32_t NavMesh::addObstacle(const Aabb& box) {
    const uint32_t id = m_nextObstacle++;
    m_obstacles[id] = {box};
    markDirty(box);
    return id;
}

bool NavMesh::moveObstacle(uint32_t id, const Aabb& box) {
    const auto found = m_obstacles.find(id);
    if (found == m_obstacles.end()) return false;
    markDirty(found->second.box);
    found->second.box = box;
    markDirty(box);
    return true;
}

bool NavMesh::removeObstacle(uint32_t id) {
    const auto found = m_obstacles.find(id);
    if (found == m_obstacles.end()) return false;
    markDirty(found->second.box);
    m_obstacles.erase(found);
    return true;
}

uint32_t NavMesh::addLink(NavLink link) {
    link.id = m_nextLink++;
    const uint32_t id = link.id;
    m_links[id] = std::move(link);
    ++m_version;
    m_linkChangeVersion = m_version;
    return id;
}

bool NavMesh::removeLink(uint32_t id) {
    if (!m_links.erase(id)) return false;
    ++m_version;
    m_linkChangeVersion = m_version;
    return true;
}

bool NavMesh::setLinkEnabled(uint32_t id, bool enabled) {
    const auto found = m_links.find(id);
    if (found == m_links.end()) return false;
    if (found->second.enabled != enabled) {
        ++m_version;
        m_linkChangeVersion = m_version;
    }
    found->second.enabled = enabled;
    return true;
}

const NavLink* NavMesh::link(uint32_t id) const {
    const auto found = m_links.find(id);
    return found == m_links.end() ? nullptr : &found->second;
}

std::vector<NavLink> NavMesh::links() const {
    std::vector<NavLink> out;
    out.reserve(m_links.size());
    for (const auto& [id, navLink] : m_links) out.push_back(navLink);
    return out;
}

void NavMesh::rebuildAll() {
    if (m_bounds.valid()) markDirty(m_bounds);
    rebuildDirty();
}

size_t NavMesh::rebuildDirty(size_t maxTiles) {
    size_t built = 0;
    while (!m_dirty.empty() && built < maxTiles) {
        const auto [tileX, tileZ] = *m_dirty.begin();
        m_dirty.erase(m_dirty.begin());
        buildTile(tileX, tileZ);
        ++built;
    }
    if (built > 0) ++m_version;
    return built;
}

const NavMesh::Tile* NavMesh::tileAt(int cx, int cz) const {
    const auto found = m_tiles.find(tileKey(tileOf(cx), tileOf(cz)));
    return found == m_tiles.end() ? nullptr : &found->second;
}

uint32_t NavMesh::layersAt(int cx, int cz, const float*& heights) const {
    const Tile* tile = tileAt(cx, cz);
    if (!tile) return 0;
    const int tileCells = m_config.tileCells;
    const int localX = cx - tileOf(cx) * tileCells;
    const int localZ = cz - tileOf(cz) * tileCells;
    const size_t column = static_cast<size_t>(localZ) * tileCells + localX;
    heights = tile->heights.data() + tile->start[column];
    return tile->start[column + 1] - tile->start[column];
}

size_t NavMesh::walkableCount() const {
    size_t count = 0;
    for (const auto& [key, tile] : m_tiles) count += tile.heights.size();
    return count;
}

uint64_t NavMesh::hash() const {
    std::vector<uint64_t> keys;
    keys.reserve(m_tiles.size());
    for (const auto& [key, tile] : m_tiles) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    uint64_t result = kFnvOffset;
    for (const uint64_t key : keys) {
        const Tile& tile = m_tiles.at(key);
        result = (result ^ key) * kFnvPrime;
        for (const uint32_t start : tile.start) result = (result ^ start) * kFnvPrime;
        for (const float height : tile.heights) result = (result ^ static_cast<uint64_t>(std::llround(height * kHashHeightScale))) * kFnvPrime;
    }
    return result;
}

glm::vec3 NavMesh::nodePosition(const NavNodeId& node) const {
    const float* heights = nullptr;
    const uint32_t count = layersAt(node.cx, node.cz, heights);
    const float y = node.layer < count ? heights[node.layer] : 0.0f;
    return glm::vec3(m_config.originX + (node.cx + 0.5f) * m_config.cellSize, y, m_config.originZ + (node.cz + 0.5f) * m_config.cellSize);
}

std::optional<NavNodeId> NavMesh::nearestNode(const glm::vec3& position, float maxDistance) const {
    if (!m_bounds.valid() || !(maxDistance >= 0.0f)) return std::nullopt;
    const int centerX = cellOf(position.x, m_config.originX);
    const int centerZ = cellOf(position.z, m_config.originZ);
    const int range = static_cast<int>(std::min(std::ceil(maxDistance / m_config.cellSize), kCellLimit));
    const int firstX = std::max(centerX - range, cellOf(m_bounds.lo.x, m_config.originX) - kBoundsCellMargin);
    const int lastX = std::min(centerX + range, cellOf(m_bounds.hi.x, m_config.originX) + kBoundsCellMargin);
    const int firstZ = std::max(centerZ - range, cellOf(m_bounds.lo.z, m_config.originZ) - kBoundsCellMargin);
    const int lastZ = std::min(centerZ + range, cellOf(m_bounds.hi.z, m_config.originZ) + kBoundsCellMargin);
    std::optional<NavNodeId> best;
    float bestDistance = maxDistance * maxDistance;
    for (int cellZ = firstZ; cellZ <= lastZ; ++cellZ) {
        for (int cellX = firstX; cellX <= lastX; ++cellX) {
            const float* heights = nullptr;
            const uint32_t count = layersAt(cellX, cellZ, heights);
            for (uint32_t layer = 0; layer < count; ++layer) {
                if (std::fabs(heights[layer] - position.y) > m_config.agentHeight) continue;
                const glm::vec3 candidate(m_config.originX + (cellX + 0.5f) * m_config.cellSize, heights[layer], m_config.originZ + (cellZ + 0.5f) * m_config.cellSize);
                const float distance = squaredDistance(candidate, position);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = NavNodeId{cellX, cellZ, layer};
                }
            }
        }
    }
    return best;
}

std::optional<glm::vec3> NavMesh::nearestPoint(const glm::vec3& position, float maxDistance) const {
    const auto node = nearestNode(position, maxDistance);
    if (!node) return std::nullopt;
    return nodePosition(*node);
}

bool NavMesh::walkable(const glm::vec3& position) const { return nearestNode(position, m_config.cellSize * kWalkableQueryCells).has_value(); }

bool NavMesh::connected(const NavNodeId& from, int dx, int dz, NavNodeId& out) const {
    const float* neighborHeights = nullptr;
    const uint32_t neighborCount = layersAt(from.cx + dx, from.cz + dz, neighborHeights);
    const float* ownHeights = nullptr;
    const uint32_t ownCount = layersAt(from.cx, from.cz, ownHeights);
    if (from.layer >= ownCount) return false;
    const float height = ownHeights[from.layer];
    int best = -1;
    float bestDifference = m_config.maxClimb + kConnectionTolerance;
    for (uint32_t layer = 0; layer < neighborCount; ++layer) {
        const float difference = std::fabs(neighborHeights[layer] - height);
        if (difference <= bestDifference) {
            bestDifference = difference;
            best = static_cast<int>(layer);
        }
    }
    if (best < 0) return false;
    out = {from.cx + dx, from.cz + dz, static_cast<uint32_t>(best)};
    return true;
}

int NavMesh::gridNeighbors(const NavNodeId& node, NavNodeId (&out)[8]) const {
    static constexpr int directions[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int count = 0;
    for (const auto& direction : directions) {
        NavNodeId neighbor;
        if (!connected(node, direction[0], direction[1], neighbor)) continue;
        if (direction[0] != 0 && direction[1] != 0) {
            NavNodeId sideX;
            NavNodeId sideZ;
            if (!connected(node, direction[0], 0, sideX) || !connected(node, 0, direction[1], sideZ)) continue;
        }
        out[count++] = neighbor;
    }
    return count;
}

bool NavMesh::lineWalkable(const glm::vec3& a, const glm::vec3& b) const {
    const auto start = nearestNode(a, m_config.cellSize * kLineStartSnapCells);
    if (!start) return false;
    const glm::vec3 delta = b - a;
    const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const float stepCount = std::ceil(distance / (m_config.cellSize * kLineStepCells));
    if (!(stepCount <= static_cast<float>(kMaxLineSteps))) return false;
    const int steps = std::max(1, static_cast<int>(stepCount));
    float lastHeight = nodePosition(*start).y;
    for (int step = 1; step <= steps; ++step) {
        const float fraction = static_cast<float>(step) / steps;
        const int cellX = cellOf(a.x + delta.x * fraction, m_config.originX);
        const int cellZ = cellOf(a.z + delta.z * fraction, m_config.originZ);
        const float* heights = nullptr;
        const uint32_t count = layersAt(cellX, cellZ, heights);
        int best = -1;
        float bestDifference = m_config.maxClimb * kLineClimbFactor;
        for (uint32_t layer = 0; layer < count; ++layer) {
            const float difference = std::fabs(heights[layer] - lastHeight);
            if (difference <= bestDifference) {
                bestDifference = difference;
                best = static_cast<int>(layer);
            }
        }
        if (best < 0) return false;
        lastHeight = heights[best];
    }
    return true;
}

void NavMesh::ensureLinks() const {
    if (m_linkGuard.builtVersion.load(std::memory_order_acquire) == m_version) return;
    std::lock_guard lock(m_linkGuard.mutex);
    if (m_linkGuard.builtVersion.load(std::memory_order_relaxed) == m_version) return;
    m_linkAdj.clear();
    for (const auto& [id, navLink] : m_links) {
        if (!navLink.enabled) continue;
        const auto nodeA = nearestNode(navLink.a, navLink.radius);
        const auto nodeB = nearestNode(navLink.b, navLink.radius);
        if (!nodeA || !nodeB) continue;
        m_linkAdj[nodeKey(*nodeA)].push_back({id, true, nodeKey(*nodeB)});
        if (navLink.bidirectional) m_linkAdj[nodeKey(*nodeB)].push_back({id, false, nodeKey(*nodeA)});
    }
    m_linkGuard.builtVersion.store(m_version, std::memory_order_release);
}
