module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr size_t kFirstWaypoint = 1;
constexpr float kMinLinkReach = 0.15f;
constexpr float kMinWaypointReach = 0.12f;
constexpr float kMinArriveSpeedFactor = 0.15f;
constexpr float kMinDirectionLength = 1.0e-6f;
constexpr float kCornerSlowDistance = 1.0f;
constexpr float kMinCornerSpeedFactor = 0.55f;
constexpr float kJamSpeed = 0.3f;
constexpr float kJamReachFactor = 1.4f;
constexpr float kJamTouchFactor = 1.1f;
constexpr float kGroundSnapCells = 1.5f;

constexpr float kSingleStepNeighborRange = 3.0f;

bool canCutCorner(const NavMesh& mesh, const NavAgent& agent, float flatDistance) {
    if (flatDistance > agent.cornerRadius || agent.waypoint + 1 >= agent.path.points.size()) return false;
    const NavPathPoint& next = agent.path.points[agent.waypoint + 1];
    if (next.link >= 0 || next.linkExit) return false;
    return mesh.lineWalkable(agent.position, next.position);
}

float cornerSpeedFactor(const NavAgent& agent, const glm::vec3& direction, float distance) {
    if (agent.waypoint + 1 >= agent.path.points.size() || distance > kCornerSlowDistance) return 1.0f;
    const NavPathPoint& next = agent.path.points[agent.waypoint + 1];
    glm::vec3 following = next.position - agent.path.points[agent.waypoint].position;
    following.y = 0.0f;
    const float length = glm::length(following);
    if (length < kMinDirectionLength) return 1.0f;
    const float alignment = glm::dot(direction, following / length);
    return kMinCornerSpeedFactor + (1.0f - kMinCornerSpeedFactor) * std::clamp(alignment * 0.5f + 0.5f, 0.0f, 1.0f);
}

bool jammedAtGoal(const NavAgent& agent, const NavNeighborSet& neighbors) {
    if (agent.waypoint + 1 != agent.path.points.size() || neighbors.count == 0) return false;
    if (glm::length(agent.velocity) > kJamSpeed) return false;
    const glm::vec3 offset = agent.path.points[agent.waypoint].position - agent.position;
    if (std::sqrt(offset.x * offset.x + offset.z * offset.z) > agent.radius * kJamReachFactor) return false;
    const NavNeighbor& nearest = neighbors.items[0];
    const float reach = (agent.radius + nearest.radius) * kJamTouchFactor;
    return neighbors.distances[0] < reach * reach;
}

void advanceWaypoints(const NavMesh& mesh, NavAgent& agent, const NavLinkTraverseFn& traverse) {
    while (agent.waypoint < agent.path.points.size()) {
        const NavPathPoint& waypoint = agent.path.points[agent.waypoint];
        const glm::vec3 offset = waypoint.position - agent.position;
        const float flatDistance = std::sqrt(offset.x * offset.x + offset.z * offset.z);
        if (waypoint.link >= 0) {
            const NavLink* link = mesh.link(static_cast<uint32_t>(waypoint.link));
            const float reach = link ? std::max(link->radius * 0.5f, kMinLinkReach) : kMinLinkReach;
            if (flatDistance > reach) return;
            const glm::vec3 exit = agent.waypoint + 1 < agent.path.points.size() ? agent.path.points[agent.waypoint + 1].position : waypoint.position;
            if (link && traverse) traverse(agent, *link, exit);
            else agent.position = exit;
            agent.velocity = glm::vec3(0.0f);
            agent.waypoint += 2;
            continue;
        }
        if (flatDistance > std::max(agent.stopDistance, kMinWaypointReach) && !canCutCorner(mesh, agent, flatDistance)) return;
        ++agent.waypoint;
    }
}

void moveAgent(const NavMesh& mesh, NavAgent& agent, float dt, const NavNeighborSet& neighbors) {
    const NavPathPoint& waypoint = agent.path.points[agent.waypoint];
    glm::vec3 offset = waypoint.position - agent.position;
    offset.y = 0.0f;
    const float distance = glm::length(offset);
    const glm::vec3 direction = distance > kMinDirectionLength ? offset / distance : glm::vec3(0.0f);
    const bool isLast = agent.waypoint + 1 >= agent.path.points.size();
    float speed = agent.maxSpeed;
    if (isLast && agent.arriveRadius > 0.0f && distance < agent.arriveRadius) speed *= std::max(distance / agent.arriveRadius, kMinArriveSpeedFactor);
    else speed *= cornerSpeedFactor(agent, direction, distance);
    glm::vec3 desired = navAvoidVelocity(agent, direction * speed, neighbors, dt);
    const float desiredLength = glm::length(desired);
    if (desiredLength > agent.maxSpeed) desired = desired * (agent.maxSpeed / desiredLength);
    glm::vec3 velocityChange = desired - agent.velocity;
    const float changeLength = glm::length(velocityChange);
    const float maxChange = agent.maxAccel * dt;
    if (changeLength > maxChange) velocityChange = velocityChange * (maxChange / changeLength);
    agent.velocity = agent.velocity + velocityChange;
    agent.velocity.y = 0.0f;
    const float step = glm::length(agent.velocity) * dt;
    glm::vec3 next = agent.position + agent.velocity * dt;
    if (isLast && distance > 0.0f && step > distance) next = agent.position + direction * distance;
    const glm::vec3 probe(next.x, agent.position.y, next.z);
    std::optional<float> groundY = mesh.groundHeight(probe);
    if (!groundY) {
        const auto ground = mesh.nearestPoint(probe, mesh.config().cellSize * kGroundSnapCells);
        if (ground) groundY = ground->y;
    }
    if (!groundY) {
        agent.velocity = glm::vec3(0.0f);
        agent.pathVersion = 0;
        return;
    }
    next.y = *groundY;
    agent.position = next;
}
}

void navAssignPath(const NavMesh& mesh, NavAgent& agent) {
    mesh.findPathInto(agent.position, agent.target, agent.path);
    agent.waypoint = kFirstWaypoint;
    agent.pathVersion = mesh.version();
    agent.state = agent.path.found ? AgentState::Moving : AgentState::Failed;
}

bool navAgentFinite(const NavAgent& agent) {
    return std::isfinite(agent.position.x) && std::isfinite(agent.position.y) && std::isfinite(agent.position.z) && std::isfinite(agent.maxSpeed) && std::isfinite(agent.maxAccel) && std::isfinite(agent.radius);
}

void navFollowPath(const NavMesh& mesh, NavAgent& agent, float dt, const NavNeighborSet& neighbors, const NavLinkTraverseFn& traverse) {
    if (agent.state == AgentState::Failed || agent.path.points.empty()) return;
    if (!navAgentFinite(agent)) {
        agent.velocity = glm::vec3(0.0f);
        agent.state = AgentState::Failed;
        return;
    }
    if (!std::isfinite(agent.velocity.x) || !std::isfinite(agent.velocity.z)) agent.velocity = glm::vec3(0.0f);
    if (!(dt >= 0.0f) || !std::isfinite(dt)) return;
    advanceWaypoints(mesh, agent, traverse);
    if (agent.waypoint < agent.path.points.size() && jammedAtGoal(agent, neighbors)) ++agent.waypoint;
    if (agent.waypoint >= agent.path.points.size()) {
        agent.velocity = glm::vec3(0.0f);
        agent.state = agent.path.partial ? AgentState::Failed : AgentState::Arrived;
        return;
    }
    moveAgent(mesh, agent, dt, neighbors);
}

void navSetTarget(const NavMesh& mesh, NavAgent& agent, const glm::vec3& target) {
    agent.target = target;
    agent.hasTarget = true;
    navAssignPath(mesh, agent);
}

void navStepAgent(const NavMesh& mesh, NavAgent& agent, float dt, const std::vector<NavAgent>* others, const NavLinkTraverseFn& traverse) {
    if (navAgentIdle(agent)) return;
    if (agent.pathVersion != mesh.version()) {
        if (mesh.pathStale(agent.path, agent.pathVersion)) {
            navAssignPath(mesh, agent);
            ++agent.repaths;
        } else {
            agent.pathVersion = mesh.version();
        }
    }
    NavNeighborSet neighbors;
    if (others) {
        for (const NavAgent& other : *others) {
            if (&other == &agent) continue;
            const glm::vec3 away = other.position - agent.position;
            const float distanceSquared = away.x * away.x + away.z * away.z;
            if (!(distanceSquared <= kSingleStepNeighborRange * kSingleStepNeighborRange) || !std::isfinite(other.velocity.x + other.velocity.z + other.radius)) continue;
            navConsiderNeighbor(neighbors, {other.position, other.velocity, other.radius}, distanceSquared);
        }
    }
    navFollowPath(mesh, agent, dt, neighbors, traverse);
}

void navStepAgents(const NavMesh& mesh, std::vector<NavAgent>& agents, float dt, const NavLinkTraverseFn& traverse) {
    thread_local NavCrowd crowd;
    crowd.step(mesh, agents, dt, traverse);
}
