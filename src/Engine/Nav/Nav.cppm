module;

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

export module Engine.Nav;

import Engine.glm;
import Engine.Geometry;
import Engine.World;

export struct NavConfig {
    float cellSize = 0.25f;
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    float maxClimb = 0.4f;
    float maxSlopeDegrees = 50.0f;
    int tileCells = 32;
    float originX = 0.0f;
    float originZ = 0.0f;
    float snapDistance = 2.0f;
    uint32_t maxExpansions = 400000;
};

export struct NavLink {
    uint32_t id = 0;
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    float radius = 0.6f;
    bool bidirectional = true;
    float cost = 0.0f;
    int userData = 0;
    bool enabled = true;
};

export struct NavPathPoint {
    glm::vec3 position{0.0f};
    int link = -1;
    bool linkExit = false;
};

export struct NavPath {
    bool found = false;
    bool partial = false;
    std::vector<NavPathPoint> points;
    float length = 0.0f;
    float gridCost = 0.0f;
    uint32_t expansions = 0;
};

export struct NavNodeId {
    int cx = 0;
    int cz = 0;
    uint32_t layer = 0;
    bool operator==(const NavNodeId&) const = default;
};

struct NavSearchTables;
struct NavPathSearch;

export class NavMesh;

export class NavPathQuery {
  public:
    NavPathQuery();
    ~NavPathQuery();
    NavPathQuery(NavPathQuery&&) noexcept;
    NavPathQuery& operator=(NavPathQuery&&) noexcept;
    [[nodiscard]] bool done() const { return m_done; }
    [[nodiscard]] const NavPath& path() const { return m_path; }
    [[nodiscard]] uint64_t version() const { return m_version; }
    [[nodiscard]] uint32_t expansions() const { return m_expansions; }
    [[nodiscard]] const glm::vec3& from() const { return m_from; }
    [[nodiscard]] const glm::vec3& to() const { return m_to; }

  private:
    friend class NavMesh;
    glm::vec3 m_from{0.0f};
    glm::vec3 m_to{0.0f};
    uint64_t m_version = 0;
    uint32_t m_expansions = 0;
    bool m_done = true;
    NavPath m_path;
    std::unique_ptr<NavSearchTables> m_tables;
    std::unique_ptr<NavPathSearch> m_search;
};

export class NavMesh {
  public:
    void configure(const NavConfig& config);
    [[nodiscard]] const NavConfig& config() const { return m_config; }

    void setGeometry(const TriangleSoup& soup);
    void buildFromWorld(World& world, uint32_t layerMask = 0xFFFFFFFFu);

    uint32_t addObstacle(const Aabb& box);
    bool moveObstacle(uint32_t id, const Aabb& box);
    bool removeObstacle(uint32_t id);
    [[nodiscard]] size_t obstacleCount() const { return m_obstacles.size(); }

    uint32_t addLink(NavLink link);
    bool removeLink(uint32_t id);
    bool setLinkEnabled(uint32_t id, bool enabled);
    [[nodiscard]] const NavLink* link(uint32_t id) const;
    [[nodiscard]] std::vector<NavLink> links() const;

    size_t rebuildDirty(size_t maxTiles = ~size_t(0));
    void rebuildAll();
    [[nodiscard]] size_t dirtyCount() const { return m_dirty.size(); }
    [[nodiscard]] size_t tileCount() const { return m_tiles.size(); }
    [[nodiscard]] size_t walkableCount() const;
    [[nodiscard]] uint64_t version() const { return m_version; }
    [[nodiscard]] uint64_t hash() const;

    [[nodiscard]] std::optional<NavNodeId> nearestNode(const glm::vec3& position, float maxDistance) const;
    [[nodiscard]] glm::vec3 nodePosition(const NavNodeId& node) const;
    int gridNeighbors(const NavNodeId& node, NavNodeId (&out)[8]) const;
    [[nodiscard]] std::optional<glm::vec3> nearestPoint(const glm::vec3& position, float maxDistance) const;
    [[nodiscard]] bool walkable(const glm::vec3& position) const;
    [[nodiscard]] bool lineWalkable(const glm::vec3& a, const glm::vec3& b) const;
    [[nodiscard]] NavPath findPath(const glm::vec3& from, const glm::vec3& to) const;
    void findPathInto(const glm::vec3& from, const glm::vec3& to, NavPath& out) const;
    void beginPath(NavPathQuery& query, const glm::vec3& from, const glm::vec3& to) const;
    bool advancePath(NavPathQuery& query, uint32_t expansionBudget) const;
    [[nodiscard]] bool pathStale(const NavPath& path, uint64_t sinceVersion) const;
    [[nodiscard]] std::optional<float> groundHeight(const glm::vec3& position) const;
    [[nodiscard]] const Bvh& bvh() const { return m_bvh; }

  private:
    struct Tile {
        uint64_t builtVersion = 0;
        std::vector<uint32_t> start;
        std::vector<float> heights;
    };
    struct Obstacle {
        Aabb box;
    };
    struct LinkEnd {
        uint32_t link = 0;
        bool fromA = true;
        uint64_t target = 0;
    };
    friend struct NavPathSearch;

    [[nodiscard]] static uint64_t tileKey(int tx, int tz);
    [[nodiscard]] static uint64_t nodeKey(const NavNodeId& n);
    [[nodiscard]] static NavNodeId nodeFromKey(uint64_t key);
    [[nodiscard]] int cellOf(float v, float origin) const;
    [[nodiscard]] int tileOf(int cell) const;
    [[nodiscard]] const Tile* tileAt(int cx, int cz) const;
    [[nodiscard]] uint32_t layersAt(int cx, int cz, const float*& heights) const;
    [[nodiscard]] bool connected(const NavNodeId& from, int dx, int dz, NavNodeId& out) const;
    void buildTile(int tx, int tz);
    void markDirty(const Aabb& box);
    void ensureLinks() const;

    NavConfig m_config;
    Bvh m_bvh;
    Aabb m_bounds;
    std::unordered_map<uint64_t, Tile> m_tiles;
    std::set<std::pair<int, int>> m_dirty;
    std::map<uint32_t, Obstacle> m_obstacles;
    std::map<uint32_t, NavLink> m_links;
    uint32_t m_nextObstacle = 1;
    uint32_t m_nextLink = 1;
    uint64_t m_version = 1;
    uint64_t m_resetVersion = 1;
    uint64_t m_linkChangeVersion = 1;
    struct LinkGuard {
        std::mutex mutex;
        std::atomic<uint64_t> builtVersion{0};
        LinkGuard() = default;
        LinkGuard(const LinkGuard&) {}
        LinkGuard& operator=(const LinkGuard&) {
            builtVersion.store(0);
            return *this;
        }
    };
    mutable LinkGuard m_linkGuard;
    mutable std::unordered_map<uint64_t, std::vector<LinkEnd>> m_linkAdj;
};

export enum class AgentState { Idle, Moving, Arrived, Failed };

export struct NavAgent {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.4f;
    float maxSpeed = 3.0f;
    float maxAccel = 12.0f;
    float arriveRadius = 1.0f;
    float cornerRadius = 0.5f;
    float stopDistance = 0.1f;
    float separationWeight = 1.5f;
    glm::vec3 target{0.0f};
    bool hasTarget = false;
    NavPath path;
    size_t waypoint = 0;
    uint64_t pathVersion = 0;
    uint32_t repaths = 0;
    AgentState state = AgentState::Idle;
};

export using NavLinkTraverseFn = std::function<void(NavAgent&, const NavLink&, const glm::vec3& exit)>;

export void navSetTarget(const NavMesh& mesh, NavAgent& agent, const glm::vec3& target);
export void navStepAgent(const NavMesh& mesh, NavAgent& agent, float dt, const std::vector<NavAgent>* others = nullptr, const NavLinkTraverseFn& traverse = {});
export void navStepAgents(const NavMesh& mesh, std::vector<NavAgent>& agents, float dt, const NavLinkTraverseFn& traverse = {});

export struct NavCrowdConfig {
    uint32_t pathExpansionBudget = ~uint32_t(0);
    uint32_t queriesPerStep = 64;
    size_t cacheEntries = 256;
    float neighborRange = 3.0f;
};

export struct NavCrowdStats {
    uint32_t expansions = 0;
    uint32_t pathsCompleted = 0;
    uint32_t cacheHits = 0;
    uint32_t pending = 0;
    uint32_t repathsSkipped = 0;
};

struct NavCrowdImpl;

export class NavCrowd {
  public:
    NavCrowd();
    explicit NavCrowd(const NavCrowdConfig& config);
    ~NavCrowd();
    NavCrowd(NavCrowd&&) noexcept;
    NavCrowd& operator=(NavCrowd&&) noexcept;

    void configure(const NavCrowdConfig& config);
    [[nodiscard]] const NavCrowdConfig& config() const;
    [[nodiscard]] const NavCrowdStats& stats() const;
    void clearCache();
    void step(const NavMesh& mesh, std::vector<NavAgent>& agents, float dt, const NavLinkTraverseFn& traverse = {});

  private:
    std::unique_ptr<NavCrowdImpl> m_impl;
};

inline constexpr int kNavMaxNeighbors = 8;

struct NavNeighbor {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.0f;
};

struct NavNeighborSet {
    NavNeighbor items[kNavMaxNeighbors];
    float distances[kNavMaxNeighbors];
    int count = 0;
};

void navConsiderNeighbor(NavNeighborSet& set, const NavNeighbor& neighbor, float distanceSquared);
glm::vec3 navOrcaVelocity(const NavAgent& agent, const glm::vec3& preferred, const NavNeighborSet& set, float dt);
glm::vec3 navAvoidVelocity(const NavAgent& agent, const glm::vec3& preferred, const NavNeighborSet& set, float dt);
bool navAgentIdle(const NavAgent& agent);
bool navAgentFinite(const NavAgent& agent);
void navAssignPath(const NavMesh& mesh, NavAgent& agent);
void navFollowPath(const NavMesh& mesh, NavAgent& agent, float dt, const NavNeighborSet& set, const NavLinkTraverseFn& traverse);
