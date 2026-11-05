module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr uint32_t kHashPrimeX = 73856093u;
constexpr uint32_t kHashPrimeZ = 19349663u;
constexpr uint32_t kHashPrimeLayer = 83492791u;
constexpr uint32_t kUnlimitedBudget = ~uint32_t(0);
constexpr size_t kMinCells = 64;
constexpr size_t kFirstWaypoint = 1;
constexpr float kCellCoordinateLimit = 1.0e6f;
constexpr float kDefaultNeighborRange = 3.0f;

int gridCell(float coordinate, float inverse) {
    const float cell = std::floor(coordinate * inverse);
    if (std::isnan(cell)) return 0;
    return static_cast<int>(std::clamp(cell, -kCellCoordinateLimit, kCellCoordinateLimit));
}
}

struct NavCrowdImpl {
    struct Pending {
        size_t index = 0;
        bool started = false;
        NavPathQuery query;
    };
    struct CacheEntry {
        uint64_t version = 0;
        NavNodeId start;
        NavNodeId goal;
        NavPath path;
        bool used = false;
    };

    NavCrowdConfig config;
    NavCrowdStats stats;
    std::vector<NavNeighbor> snapshot;
    std::vector<uint32_t> cellOfAgent;
    std::vector<uint32_t> cellStart;
    std::vector<uint32_t> cellItems;
    std::vector<uint8_t> queued;
    std::vector<Pending> pending;
    std::vector<NavPathQuery> idleQueries;
    std::vector<CacheEntry> cache;
    uint32_t cellMask = 0;

    static uint32_t hashCell(int cellX, int cellZ) { return (static_cast<uint32_t>(cellX) * kHashPrimeX) ^ (static_cast<uint32_t>(cellZ) * kHashPrimeZ); }

    void buildGrid(const std::vector<NavAgent>& agents) {
        const size_t count = agents.size();
        snapshot.resize(count);
        cellOfAgent.resize(count);
        size_t cells = kMinCells;
        while (cells < count * 2) cells *= 2;
        cellMask = static_cast<uint32_t>(cells - 1);
        cellStart.assign(cells + 1, 0);
        cellItems.resize(count);
        const float inverse = 1.0f / config.neighborRange;
        for (size_t index = 0; index < count; ++index) {
            const NavAgent& agent = agents[index];
            snapshot[index] = {agent.position, agent.velocity, agent.radius};
            const uint32_t cell = hashCell(gridCell(agent.position.x, inverse), gridCell(agent.position.z, inverse)) & cellMask;
            cellOfAgent[index] = cell;
            ++cellStart[cell + 1];
        }
        for (size_t cell = 0; cell < cells; ++cell) cellStart[cell + 1] += cellStart[cell];
        for (size_t index = 0; index < count; ++index) cellItems[cellStart[cellOfAgent[index]]++] = static_cast<uint32_t>(index);
        for (size_t cell = cells; cell > 0; --cell) cellStart[cell] = cellStart[cell - 1];
        cellStart[0] = 0;
    }

    void gather(size_t self, NavNeighborSet& set) const {
        const NavNeighbor& me = snapshot[self];
        const float inverse = 1.0f / config.neighborRange;
        const int centerX = gridCell(me.position.x, inverse);
        const int centerZ = gridCell(me.position.z, inverse);
        const float rangeSquared = config.neighborRange * config.neighborRange;
        for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                const uint32_t cell = hashCell(centerX + offsetX, centerZ + offsetZ) & cellMask;
                for (uint32_t slot = cellStart[cell]; slot < cellStart[cell + 1]; ++slot) {
                    const uint32_t other = cellItems[slot];
                    if (other == self) continue;
                    const NavNeighbor& neighbor = snapshot[other];
                    const float deltaX = neighbor.position.x - me.position.x;
                    const float deltaZ = neighbor.position.z - me.position.z;
                    const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
                    if (!(distanceSquared <= rangeSquared) || !std::isfinite(neighbor.velocity.x + neighbor.velocity.z + neighbor.radius)) continue;
                    navConsiderNeighbor(set, neighbor, distanceSquared);
                }
            }
        }
    }

    CacheEntry* cacheSlot(const NavNodeId& start, const NavNodeId& goal) {
        if (cache.empty()) return nullptr;
        const uint32_t hash = hashCell(start.cx, start.cz) ^ (hashCell(goal.cx, goal.cz) * 31u) ^ (start.layer * kHashPrimeLayer) ^ (goal.layer * kHashPrimeLayer * 7u);
        return &cache[hash % cache.size()];
    }

    bool applyCached(const NavMesh& mesh, NavAgent& agent) {
        const auto start = mesh.nearestNode(agent.position, mesh.config().snapDistance);
        const auto goal = mesh.nearestNode(agent.target, mesh.config().snapDistance);
        if (!start || !goal) return false;
        CacheEntry* entry = cacheSlot(*start, *goal);
        if (!entry || !entry->used || entry->version != mesh.version() || !(entry->start == *start) || !(entry->goal == *goal)) return false;
        agent.path.found = entry->path.found;
        agent.path.partial = entry->path.partial;
        agent.path.length = entry->path.length;
        agent.path.expansions = 0;
        agent.path.points.assign(entry->path.points.begin(), entry->path.points.end());
        agent.path.points.front().position = agent.position;
        if (!agent.path.partial && agent.path.points.size() > 1) agent.path.points.back().position = agent.target;
        ++stats.cacheHits;
        return true;
    }

    void store(const NavMesh& mesh, const NavAgent& agent, const NavPath& path) {
        if (!path.found || cache.empty()) return;
        const auto start = mesh.nearestNode(agent.position, mesh.config().snapDistance);
        const auto goal = mesh.nearestNode(agent.target, mesh.config().snapDistance);
        if (!start || !goal) return;
        CacheEntry* entry = cacheSlot(*start, *goal);
        entry->used = true;
        entry->version = mesh.version();
        entry->start = *start;
        entry->goal = *goal;
        entry->path.found = path.found;
        entry->path.partial = path.partial;
        entry->path.length = path.length;
        entry->path.points.assign(path.points.begin(), path.points.end());
    }

    void finishPath(const NavMesh& mesh, NavAgent& agent) {
        agent.waypoint = kFirstWaypoint;
        agent.pathVersion = mesh.version();
        agent.state = agent.path.found ? AgentState::Moving : AgentState::Failed;
        ++agent.repaths;
        ++stats.pathsCompleted;
    }

    void repathNow(const NavMesh& mesh, NavAgent& agent) {
        if (!applyCached(mesh, agent)) {
            mesh.findPathInto(agent.position, agent.target, agent.path);
            stats.expansions += agent.path.expansions;
            store(mesh, agent, agent.path);
        }
        finishPath(mesh, agent);
    }

    void enqueue(size_t index) {
        if (queued[index]) return;
        queued[index] = 1;
        Pending entry;
        entry.index = index;
        if (!idleQueries.empty()) {
            entry.query = std::move(idleQueries.back());
            idleQueries.pop_back();
        }
        pending.push_back(std::move(entry));
    }

    void service(const NavMesh& mesh, std::vector<NavAgent>& agents) {
        uint32_t remaining = config.pathExpansionBudget;
        uint32_t begins = config.queriesPerStep;
        size_t keep = 0;
        for (size_t slot = 0; slot < pending.size(); ++slot) {
            Pending& entry = pending[slot];
            NavAgent& agent = agents[entry.index];
            bool finished = false;
            if (!agent.hasTarget) {
                finished = true;
            } else if (remaining > 0 && (entry.started || begins > 0)) {
                if (!entry.started) {
                    --begins;
                    entry.started = true;
                    if (applyCached(mesh, agent)) {
                        finishPath(mesh, agent);
                        finished = true;
                    } else {
                        mesh.beginPath(entry.query, agent.position, agent.target);
                    }
                }
                if (!finished) {
                    const uint32_t before = entry.query.expansions();
                    const bool done = mesh.advancePath(entry.query, remaining);
                    const uint32_t spent = entry.query.expansions() >= before ? entry.query.expansions() - before : entry.query.expansions();
                    remaining = spent >= remaining ? 0 : remaining - spent;
                    stats.expansions += spent;
                    if (done && !(entry.query.to() == agent.target)) {
                        finished = true;
                    } else if (done) {
                        const NavPath& result = entry.query.path();
                        agent.path.found = result.found;
                        agent.path.partial = result.partial;
                        agent.path.length = result.length;
                        agent.path.expansions = result.expansions;
                        agent.path.points.assign(result.points.begin(), result.points.end());
                        store(mesh, agent, agent.path);
                        finishPath(mesh, agent);
                        finished = true;
                    }
                }
            }
            if (!finished && entry.started && !(entry.query.to() == agent.target)) finished = true;
            if (finished) {
                queued[entry.index] = 0;
                idleQueries.push_back(std::move(entry.query));
            } else {
                if (keep != slot) pending[keep] = std::move(entry);
                ++keep;
            }
        }
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(keep), pending.end());
        stats.pending = static_cast<uint32_t>(pending.size());
    }
};

NavCrowd::NavCrowd() : NavCrowd(NavCrowdConfig{}) {}

NavCrowd::NavCrowd(const NavCrowdConfig& config) : m_impl(std::make_unique<NavCrowdImpl>()) { configure(config); }

NavCrowd::~NavCrowd() = default;
NavCrowd::NavCrowd(NavCrowd&&) noexcept = default;
NavCrowd& NavCrowd::operator=(NavCrowd&&) noexcept = default;

void NavCrowd::configure(const NavCrowdConfig& config) {
    m_impl->config = config;
    if (!(config.neighborRange > 0.0f) || !std::isfinite(config.neighborRange)) m_impl->config.neighborRange = kDefaultNeighborRange;
    m_impl->cache.assign(config.cacheEntries, NavCrowdImpl::CacheEntry{});
}

const NavCrowdConfig& NavCrowd::config() const { return m_impl->config; }

const NavCrowdStats& NavCrowd::stats() const { return m_impl->stats; }

void NavCrowd::clearCache() {
    for (NavCrowdImpl::CacheEntry& entry : m_impl->cache) entry.used = false;
}

void NavCrowd::step(const NavMesh& mesh, std::vector<NavAgent>& agents, float dt, const NavLinkTraverseFn& traverse) {
    NavCrowdImpl& impl = *m_impl;
    impl.stats = NavCrowdStats{};
    impl.buildGrid(agents);
    if (impl.queued.size() != agents.size()) {
        impl.queued.assign(agents.size(), 0);
        impl.pending.clear();
    }
    const bool unlimited = impl.config.pathExpansionBudget == kUnlimitedBudget;
    for (size_t index = 0; index < agents.size(); ++index) {
        NavAgent& agent = agents[index];
        if (navAgentIdle(agent)) continue;
        if (agent.pathVersion != mesh.version() && !impl.queued[index]) {
            if (!mesh.pathStale(agent.path, agent.pathVersion)) {
                agent.pathVersion = mesh.version();
                ++impl.stats.repathsSkipped;
            } else if (unlimited) {
                impl.repathNow(mesh, agent);
            } else {
                impl.enqueue(index);
            }
        }
        NavNeighborSet neighbors;
        impl.gather(index, neighbors);
        navFollowPath(mesh, agent, dt, neighbors, traverse);
    }
    if (!unlimited) impl.service(mesh, agents);
}
