module;

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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
    uint32_t expansions = 0;
};

export struct NavNodeId {
    int cx = 0;
    int cz = 0;
    uint32_t layer = 0;
    bool operator==(const NavNodeId&) const = default;
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
    [[nodiscard]] std::optional<glm::vec3> nearestPoint(const glm::vec3& position, float maxDistance) const;
    [[nodiscard]] bool walkable(const glm::vec3& position) const;
    [[nodiscard]] bool lineWalkable(const glm::vec3& a, const glm::vec3& b) const;
    [[nodiscard]] NavPath findPath(const glm::vec3& from, const glm::vec3& to) const;
    [[nodiscard]] const Bvh& bvh() const { return m_bvh; }

  private:
    struct Tile {
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
    mutable uint64_t m_linkVersion = 0;
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
