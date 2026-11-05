module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr float kCostEpsilon = 1.0e-6f;
constexpr float kLinkBaseCost = 0.1f;
constexpr size_t kSmoothingWindow = 48;
constexpr float kHeuristicScale = 1.0005f;
constexpr float kDiagonalSaving = 0.41421356f;
constexpr float kConnectionTolerance = 1.0e-4f;
constexpr int kNoTile = std::numeric_limits<int>::min();
constexpr uint32_t kProbeThreshold = 1500;
struct Column {
    const float* heights = nullptr;
    uint32_t count = 0;
};
constexpr int kNeighborDirections[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
}

#include "NavSearchTables.hpp"

enum class NavSearchStatus { Pending, Found, Failed };

struct NavPathSearch {
    using Record = NavSearchTables::Record;
    using Item = NavSearchTables::Item;

    const NavMesh& mesh;
    NavSearchTables& tables;
    NavNodeId startNode;
    uint64_t startKey;
    uint64_t goalKey;
    glm::vec3 goalPosition;
    NavPath result;
    uint64_t bestKey;
    float bestHeuristic = 0.0f;
    bool started = false;
    bool octile = false;
    const NavMesh::Tile* cachedTile = nullptr;
    int cachedTileX = kNoTile;
    int cachedTileZ = kNoTile;

    NavPathSearch(const NavMesh& navMesh, NavSearchTables& searchTables, const NavNodeId& start, const NavNodeId& goal)
        : mesh(navMesh), tables(searchTables), startNode(start), startKey(NavMesh::nodeKey(start)), goalKey(NavMesh::nodeKey(goal)), goalPosition(navMesh.nodePosition(goal)), bestKey(startKey), octile(navMesh.m_linkAdj.empty()) {
        tables.reset();
    }

    float heuristic(const glm::vec3& position) const {
        if (!octile) return glm::length(goalPosition - position);
        const float deltaX = std::fabs(goalPosition.x - position.x);
        const float deltaZ = std::fabs(goalPosition.z - position.z);
        return kHeuristicScale * (std::max(deltaX, deltaZ) + kDiagonalSaving * std::min(deltaX, deltaZ));
    }

    void pushOpen(const Item& item) {
        tables.heap.push_back(item);
        std::push_heap(tables.heap.begin(), tables.heap.end());
    }

    void relax(uint64_t parentKey, float parentCost, uint64_t key, const glm::vec3& position, float stepCost, int link, bool fromA) {
        const float newCost = parentCost + stepCost;
        const Record* existing = tables.find(key);
        if (existing && (existing->closed || existing->cost <= newCost)) return;
        tables.insert(key) = {newCost, parentKey, link, fromA, false};
        pushOpen({newCost + heuristic(position), newCost, key});
    }

    Column column(int cellX, int cellZ) {
        const int tileX = mesh.tileOf(cellX);
        const int tileZ = mesh.tileOf(cellZ);
        if (tileX != cachedTileX || tileZ != cachedTileZ || cachedTileX == kNoTile) {
            cachedTileX = tileX;
            cachedTileZ = tileZ;
            const auto found = mesh.m_tiles.find(NavMesh::tileKey(tileX, tileZ));
            cachedTile = found == mesh.m_tiles.end() ? nullptr : &found->second;
        }
        if (!cachedTile) return {};
        const int tileCells = mesh.m_config.tileCells;
        const size_t index = static_cast<size_t>(cellZ - tileZ * tileCells) * tileCells + (cellX - tileX * tileCells);
        const uint32_t first = cachedTile->start[index];
        return {cachedTile->heights.data() + first, cachedTile->start[index + 1] - first};
    }

    int bestLayer(const Column& neighbor, float height) const {
        int best = -1;
        float bestDifference = mesh.m_config.maxClimb + kConnectionTolerance;
        for (uint32_t layer = 0; layer < neighbor.count; ++layer) {
            const float difference = std::fabs(neighbor.heights[layer] - height);
            if (difference <= bestDifference) {
                bestDifference = difference;
                best = static_cast<int>(layer);
            }
        }
        return best;
    }

    glm::vec3 cellPosition(int cellX, int cellZ, float height) const {
        return glm::vec3(mesh.m_config.originX + (cellX + 0.5f) * mesh.m_config.cellSize, height, mesh.m_config.originZ + (cellZ + 0.5f) * mesh.m_config.cellSize);
    }

    void expandGrid(const NavNodeId& node, const glm::vec3& position, uint64_t key, float cost) {
        int layers[8];
        for (int direction = 0; direction < 4; ++direction) {
            const int* offset = kNeighborDirections[direction];
            layers[direction] = bestLayer(column(node.cx + offset[0], node.cz + offset[1]), position.y);
        }
        for (int direction = 4; direction < 8; ++direction) {
            const int* offset = kNeighborDirections[direction];
            const int sideX = offset[0] > 0 ? 0 : 1;
            const int sideZ = offset[1] > 0 ? 2 : 3;
            layers[direction] = layers[sideX] >= 0 && layers[sideZ] >= 0 ? bestLayer(column(node.cx + offset[0], node.cz + offset[1]), position.y) : -1;
        }
        for (int direction = 0; direction < 8; ++direction) {
            if (layers[direction] < 0) continue;
            const int cellX = node.cx + kNeighborDirections[direction][0];
            const int cellZ = node.cz + kNeighborDirections[direction][1];
            const float height = column(cellX, cellZ).heights[layers[direction]];
            const glm::vec3 neighborPosition = cellPosition(cellX, cellZ, height);
            const NavNodeId neighbor{cellX, cellZ, static_cast<uint32_t>(layers[direction])};
            relax(key, cost, NavMesh::nodeKey(neighbor), neighborPosition, glm::length(neighborPosition - position), -1, true);
        }
    }

    void expandLinks(uint64_t key, float cost) {
        const auto adjacency = mesh.m_linkAdj.find(key);
        if (adjacency == mesh.m_linkAdj.end()) return;
        for (const NavMesh::LinkEnd& end : adjacency->second) {
            const NavLink& link = mesh.m_links.at(end.link);
            relax(key, cost, end.target, mesh.nodePosition(NavMesh::nodeFromKey(end.target)), kLinkBaseCost + link.cost, static_cast<int>(end.link), end.fromA);
        }
    }

    bool goalIsolated() {
        NavVisited& visited = tables.visited;
        visited.reset();
        visited.insert(goalKey);
        visited.queue.push_back(goalKey);
        NavNodeId neighbors[8];
        for (size_t head = 0; head < visited.queue.size(); ++head) {
            const int count = mesh.gridNeighbors(NavMesh::nodeFromKey(visited.queue[head]), neighbors);
            for (int index = 0; index < count; ++index) {
                const uint64_t key = NavMesh::nodeKey(neighbors[index]);
                if (key == startKey) return false;
                if (!visited.insert(key)) continue;
                if (visited.queue.size() >= kProbeLimit) return false;
                visited.queue.push_back(key);
            }
        }
        return true;
    }

    NavSearchStatus search(uint32_t sliceBudget) {
        if (!started) {
            started = true;
            bestHeuristic = heuristic(mesh.nodePosition(startNode));
            tables.insert(startKey) = {0.0f, startKey, -1, true, false};
            pushOpen({bestHeuristic, 0.0f, startKey});
        }
        uint32_t spent = 0;
        while (!tables.heap.empty() && result.expansions < mesh.m_config.maxExpansions) {
            if (spent >= sliceBudget) return NavSearchStatus::Pending;
            std::pop_heap(tables.heap.begin(), tables.heap.end());
            const Item item = tables.heap.back();
            tables.heap.pop_back();
            Record* record = tables.find(item.key);
            if (record->closed || item.cost > record->cost + kCostEpsilon) continue;
            record->closed = true;
            const float cost = record->cost;
            ++result.expansions;
            ++spent;
            if (item.key == goalKey) return NavSearchStatus::Found;
            if (result.expansions == kProbeThreshold && octile && goalIsolated()) return NavSearchStatus::Failed;
            const NavNodeId node = NavMesh::nodeFromKey(item.key);
            const glm::vec3 position = mesh.nodePosition(node);
            const float remaining = heuristic(position);
            if (remaining < bestHeuristic) {
                bestHeuristic = remaining;
                bestKey = item.key;
            }
            expandGrid(node, position, item.key, cost);
            expandLinks(item.key, cost);
        }
        return NavSearchStatus::Failed;
    }

    void chainTo(uint64_t endKey) {
        std::vector<uint64_t>& chain = tables.chain;
        chain.clear();
        for (uint64_t key = endKey;; key = tables.find(key)->parent) {
            chain.push_back(key);
            if (key == startKey) break;
        }
        std::reverse(chain.begin(), chain.end());
    }

    void rawPoints(const glm::vec3& from, const glm::vec3& to, uint64_t endKey) {
        std::vector<NavPathPoint>& raw = tables.raw;
        raw.clear();
        raw.push_back({from, -1, false});
        for (size_t index = 0; index < tables.chain.size(); ++index) {
            const Record& record = *tables.find(tables.chain[index]);
            if (index > 0 && record.link >= 0) {
                const NavLink& link = mesh.m_links.at(static_cast<uint32_t>(record.link));
                raw.push_back({record.fromA ? link.a : link.b, record.link, false});
                raw.push_back({record.fromA ? link.b : link.a, -1, true});
            } else {
                raw.push_back({mesh.nodePosition(NavMesh::nodeFromKey(tables.chain[index])), -1, false});
            }
        }
        raw.push_back({result.partial ? mesh.nodePosition(NavMesh::nodeFromKey(endKey)) : to, -1, false});
    }

    void smooth() {
        const std::vector<NavPathPoint>& raw = tables.raw;
        std::vector<NavPathPoint>& out = tables.smoothed;
        out.clear();
        out.push_back(raw[0]);
        size_t current = 0;
        while (current + 1 < raw.size()) {
            if (raw[current].link >= 0) {
                out.push_back(raw[current + 1]);
                current += 1;
                continue;
            }
            size_t firstBlocked = raw.size();
            for (size_t index = current + 1; index < raw.size(); ++index) {
                if (raw[index].link >= 0 || raw[index].linkExit) {
                    firstBlocked = index;
                    break;
                }
            }
            size_t farthest = current + 1;
            const size_t windowEnd = std::min({raw.size() - 1, current + kSmoothingWindow, firstBlocked - 1});
            for (size_t candidate = windowEnd; candidate > current + 1; --candidate) {
                if (mesh.lineWalkable(raw[current].position, raw[candidate].position)) {
                    farthest = candidate;
                    break;
                }
            }
            out.push_back(raw[farthest]);
            current = farthest;
        }
    }

    void finish(NavSearchStatus status, const glm::vec3& from, const glm::vec3& to, NavPath& out) {
        out.found = false;
        out.partial = false;
        out.length = 0.0f;
        out.expansions = result.expansions;
        out.points.clear();
        uint64_t endKey = goalKey;
        if (status != NavSearchStatus::Found) {
            if (bestKey == startKey && startKey != goalKey) return;
            endKey = bestKey;
            result.partial = true;
        }
        out.found = true;
        out.partial = result.partial;
        out.gridCost = tables.find(endKey)->cost;
        chainTo(endKey);
        rawPoints(from, to, endKey);
        smooth();
        const std::vector<NavPathPoint>& points = tables.smoothed;
        for (size_t index = 1; index < points.size(); ++index) {
            if (!points[index].linkExit) out.length += glm::length(points[index].position - points[index - 1].position);
        }
        out.points.assign(points.begin(), points.end());
    }
};

NavPathQuery::NavPathQuery() = default;
NavPathQuery::~NavPathQuery() = default;
NavPathQuery::NavPathQuery(NavPathQuery&&) noexcept = default;
NavPathQuery& NavPathQuery::operator=(NavPathQuery&&) noexcept = default;

void NavMesh::findPathInto(const glm::vec3& from, const glm::vec3& to, NavPath& out) const {
    thread_local NavSearchTables tables;
    ensureLinks();
    out.found = false;
    out.partial = false;
    out.length = 0.0f;
    out.gridCost = 0.0f;
    out.expansions = 0;
    out.points.clear();
    const auto start = nearestNode(from, m_config.snapDistance);
    const auto goal = nearestNode(to, m_config.snapDistance);
    if (!start || !goal) return;
    NavPathSearch search(*this, tables, *start, *goal);
    const NavSearchStatus status = search.search(~uint32_t(0));
    search.finish(status, from, to, out);
}

NavPath NavMesh::findPath(const glm::vec3& from, const glm::vec3& to) const {
    NavPath path;
    findPathInto(from, to, path);
    return path;
}

void NavMesh::beginPath(NavPathQuery& query, const glm::vec3& from, const glm::vec3& to) const {
    ensureLinks();
    query.m_from = from;
    query.m_to = to;
    query.m_version = m_version;
    query.m_done = false;
    query.m_path = NavPath{};
    query.m_search.reset();
    const auto start = nearestNode(from, m_config.snapDistance);
    const auto goal = nearestNode(to, m_config.snapDistance);
    if (!start || !goal) {
        query.m_done = true;
        return;
    }
    if (!query.m_tables) query.m_tables = std::make_unique<NavSearchTables>();
    query.m_search = std::make_unique<NavPathSearch>(*this, *query.m_tables, *start, *goal);
}

bool NavMesh::advancePath(NavPathQuery& query, uint32_t expansionBudget) const {
    if (query.m_done) return true;
    if (query.m_version != m_version) beginPath(query, query.m_from, query.m_to);
    if (query.m_done) return true;
    const NavSearchStatus status = query.m_search->search(expansionBudget);
    query.m_expansions = query.m_search->result.expansions;
    if (status == NavSearchStatus::Pending) return false;
    query.m_search->finish(status, query.m_from, query.m_to, query.m_path);
    query.m_search.reset();
    query.m_done = true;
    return true;
}
