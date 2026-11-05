module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr float kSideBiasCosine = 0.9950042f;
constexpr float kSideBiasSine = 0.0998334f;
}

bool navAgentIdle(const NavAgent& agent) { return !agent.hasTarget || agent.state == AgentState::Arrived || agent.state == AgentState::Idle; }

void navConsiderNeighbor(NavNeighborSet& set, const NavNeighbor& neighbor, float distanceSquared) {
    int slot = set.count;
    if (set.count == kNavMaxNeighbors) {
        if (distanceSquared >= set.distances[kNavMaxNeighbors - 1]) return;
        slot = kNavMaxNeighbors - 1;
    } else {
        ++set.count;
    }
    while (slot > 0 && set.distances[slot - 1] > distanceSquared) {
        set.items[slot] = set.items[slot - 1];
        set.distances[slot] = set.distances[slot - 1];
        --slot;
    }
    set.items[slot] = neighbor;
    set.distances[slot] = distanceSquared;
}

glm::vec3 navAvoidVelocity(const NavAgent& agent, const glm::vec3& preferred, const NavNeighborSet& set, float dt) {
    if (set.count == 0) return preferred;
    const glm::vec3 biased(preferred.x * kSideBiasCosine - preferred.z * kSideBiasSine, 0.0f, preferred.x * kSideBiasSine + preferred.z * kSideBiasCosine);
    return navOrcaVelocity(agent, biased, set, dt);
}
