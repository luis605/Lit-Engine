#include "TestSupport.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <queue>
#include <utility>
#include <vector>

import Engine.Nav;
import Engine.Geometry;
import Engine.glm;
import Engine.Rng;

namespace {

constexpr float kStep = 1.0f / 30.0f;
constexpr float kArenaHalf = 40.0f;

NavMesh makeArena(float half) {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(half, 0.5f, half));
    NavMesh mesh;
    NavConfig config;
    config.originX = -half;
    config.originZ = -half;
    config.cellSize = 0.5f;
    mesh.configure(config);
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    return mesh;
}

Aabb boxAt(float x, float z, float half) {
    Aabb box;
    box.grow(glm::vec3(x - half, 0.0f, z - half));
    box.grow(glm::vec3(x + half, 2.0f, z + half));
    return box;
}

glm::vec3 randomGround(Pcg32& rng, float half) { return glm::vec3((rng.uniform() - 0.5f) * 2.0f * half, 0.0f, (rng.uniform() - 0.5f) * 2.0f * half); }

std::vector<NavAgent> makeAgents(const NavMesh& mesh, size_t count, float half, uint64_t seed) {
    Pcg32 rng(seed);
    std::vector<NavAgent> agents(count);
    for (NavAgent& agent : agents) {
        agent.position = randomGround(rng, half);
        navSetTarget(mesh, agent, randomGround(rng, half));
    }
    return agents;
}

uint64_t hashAgents(const std::vector<NavAgent>& agents) {
    uint64_t result = 1469598103934665603ULL;
    for (const NavAgent& agent : agents) {
        for (const float value : {agent.position.x, agent.position.y, agent.position.z, agent.velocity.x, agent.velocity.z}) {
            result = (result ^ static_cast<uint64_t>(std::llround(value * 1000.0f))) * 1099511628211ULL;
        }
    }
    return result;
}

void testSlicedMatchesSync() {
    NavMesh mesh = makeArena(kArenaHalf);
    const glm::vec3 from(-30.0f, 0.0f, -30.0f);
    const glm::vec3 to(30.0f, 0.0f, 28.0f);
    const NavPath direct = mesh.findPath(from, to);
    NavPathQuery query;
    mesh.beginPath(query, from, to);
    int slices = 0;
    while (!mesh.advancePath(query, 40)) ++slices;
    CHECK(slices > 2);
    CHECK(query.path().found && query.path().points.size() == direct.points.size());
    CHECK(std::fabs(query.path().length - direct.length) < 1.0e-3f);
}

void testLocalInvalidation() {
    NavMesh mesh = makeArena(kArenaHalf);
    std::vector<NavAgent> agents(2);
    agents[0].position = glm::vec3(-30.0f, 0.0f, 0.0f);
    agents[1].position = glm::vec3(-30.0f, 0.0f, 30.0f);
    navSetTarget(mesh, agents[0], glm::vec3(30.0f, 0.0f, 0.0f));
    navSetTarget(mesh, agents[1], glm::vec3(-10.0f, 0.0f, 30.0f));
    for (int step = 0; step < 5; ++step) navStepAgents(mesh, agents, kStep);
    const uint32_t before0 = agents[0].repaths;
    const uint32_t before1 = agents[1].repaths;
    const uint32_t obstacle = mesh.addObstacle(boxAt(0.0f, 0.0f, 1.0f));
    CHECK(mesh.dirtyCount() > 0 && mesh.dirtyCount() < mesh.tileCount());
    mesh.rebuildDirty();
    navStepAgents(mesh, agents, kStep);
    CHECK(agents[0].repaths == before0 + 1);
    CHECK(agents[1].repaths == before1);
    for (int step = 0; step < 900; ++step) navStepAgents(mesh, agents, kStep);
    CHECK(agents[0].state == AgentState::Arrived);
    CHECK(mesh.moveObstacle(obstacle, boxAt(0.0f, 20.0f, 1.0f)));
    mesh.rebuildDirty();
    CHECK(mesh.removeObstacle(obstacle));
    mesh.rebuildDirty();
    CHECK(mesh.dirtyCount() == 0);
}

void testBudgetedPaths() {
    NavMesh mesh = makeArena(kArenaHalf);
    std::vector<NavAgent> agents = makeAgents(mesh, 40, 30.0f, 11);
    NavCrowdConfig config;
    config.pathExpansionBudget = 60;
    NavCrowd crowd(config);
    mesh.addObstacle(boxAt(5.0f, 5.0f, 3.0f));
    mesh.rebuildDirty();
    for (NavAgent& agent : agents) agent.pathVersion = 0;
    uint32_t maxSpent = 0;
    uint32_t peakPending = 0;
    int steps = 0;
    do {
        crowd.step(mesh, agents, kStep);
        maxSpent = std::max(maxSpent, crowd.stats().expansions);
        peakPending = std::max(peakPending, crowd.stats().pending);
        ++steps;
    } while (crowd.stats().pending > 0 && steps < 2000);
    CHECK(peakPending > 0 && steps > 1);
    CHECK(crowd.stats().pending == 0);
    CHECK(maxSpent <= config.pathExpansionBudget + 8);
    for (const NavAgent& agent : agents) CHECK(agent.pathVersion == mesh.version());
}

void testPathCache() {
    NavMesh mesh = makeArena(kArenaHalf);
    std::vector<NavAgent> agents(30);
    for (size_t index = 0; index < agents.size(); ++index) {
        agents[index].position = glm::vec3(-30.0f + 0.01f * static_cast<float>(index), 0.0f, -30.0f);
        agents[index].target = glm::vec3(30.0f, 0.0f, 30.0f);
        agents[index].hasTarget = true;
        agents[index].state = AgentState::Moving;
    }
    NavCrowd crowd;
    crowd.step(mesh, agents, kStep);
    CHECK(crowd.stats().pathsCompleted == 30);
    CHECK(crowd.stats().cacheHits >= 25);
    CHECK(agents[7].state == AgentState::Moving && agents[7].path.found);
}

void runCounterflow(float laneSpacing, size_t requiredArrivals, float requiredGap) {
    NavMesh mesh = makeArena(kArenaHalf);
    std::vector<NavAgent> agents(40);
    for (size_t index = 0; index < agents.size(); ++index) {
        const bool left = index % 2 == 0;
        const float lane = (static_cast<float>(index / 2) - 10.0f) * laneSpacing;
        agents[index].position = glm::vec3(left ? -10.0f : 10.0f, 0.0f, lane);
        navSetTarget(mesh, agents[index], glm::vec3(left ? 10.0f : -10.0f, 0.0f, lane));
    }
    float minGap = 1.0e9f;
    for (int step = 0; step < 600; ++step) {
        navStepAgents(mesh, agents, kStep);
        for (size_t first = 0; first < agents.size(); ++first) {
            for (size_t second = first + 1; second < agents.size(); ++second) {
                minGap = std::min(minGap, glm::length(agents[first].position - agents[second].position) / (agents[first].radius + agents[second].radius));
            }
        }
    }
    size_t arrived = 0;
    for (const NavAgent& agent : agents) arrived += agent.state == AgentState::Arrived ? 1 : 0;
    std::printf("lane %.2f minGap %.3f arrived %zu\n", laneSpacing, minGap, arrived);
    CHECK(minGap > requiredGap);
    CHECK(arrived >= requiredArrivals);
}

void testAvoidance() {
    runCounterflow(1.0f, 40, 0.6f);
    runCounterflow(0.7f, 36, 0.5f);
}

float dijkstraCost(const NavMesh& mesh, const NavNodeId& start, const NavNodeId& goal) {
    using Entry = std::pair<float, uint64_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::map<uint64_t, float> best;
    std::map<uint64_t, NavNodeId> nodes;
    const auto keyOf = [](const NavNodeId& node) { return (static_cast<uint64_t>(node.cx + 100000) << 40) | (static_cast<uint64_t>(node.cz + 100000) << 8) | node.layer; };
    best[keyOf(start)] = 0.0f;
    nodes[keyOf(start)] = start;
    open.push({0.0f, keyOf(start)});
    while (!open.empty()) {
        const auto [cost, key] = open.top();
        open.pop();
        if (cost > best[key]) continue;
        const NavNodeId node = nodes[key];
        if (node == goal) return cost;
        NavNodeId neighbors[8];
        const int count = mesh.gridNeighbors(node, neighbors);
        for (int index = 0; index < count; ++index) {
            const float next = cost + glm::length(mesh.nodePosition(neighbors[index]) - mesh.nodePosition(node));
            const uint64_t neighborKey = keyOf(neighbors[index]);
            const auto found = best.find(neighborKey);
            if (found != best.end() && found->second <= next) continue;
            best[neighborKey] = next;
            nodes[neighborKey] = neighbors[index];
            open.push({next, neighborKey});
        }
    }
    return -1.0f;
}

void testOptimalVersusDijkstra() {
    Pcg32 rng(77);
    NavMesh mesh = makeArena(20.0f);
    for (int obstacle = 0; obstacle < 25; ++obstacle) mesh.addObstacle(boxAt((rng.uniform() - 0.5f) * 34.0f, (rng.uniform() - 0.5f) * 34.0f, 0.5f + rng.uniform() * 2.0f));
    mesh.rebuildDirty();
    int compared = 0;
    for (int trial = 0; trial < 60; ++trial) {
        const glm::vec3 from = randomGround(rng, 18.0f);
        const glm::vec3 to = randomGround(rng, 18.0f);
        const auto start = mesh.nearestNode(from, mesh.config().snapDistance);
        const auto goal = mesh.nearestNode(to, mesh.config().snapDistance);
        if (!start || !goal) continue;
        const float reference = dijkstraCost(mesh, *start, *goal);
        const NavPath path = mesh.findPath(from, to);
        if (reference < 0.0f) {
            CHECK(!path.found || path.partial);
            continue;
        }
        CHECK(path.found && !path.partial);
        CHECK(std::fabs(path.gridCost - reference) <= 1.0e-3f * std::max(1.0f, reference));
        ++compared;
    }
    CHECK(compared > 40);
}

void testPathSpeed() {
    NavMesh mesh = makeArena(kArenaHalf);
    uint32_t expansions = 0;
    for (int trial = 0; trial < 20; ++trial) {
        const NavPath path = mesh.findPath(glm::vec3(-35.0f, 0.0f, -30.0f + trial), glm::vec3(35.0f, 0.0f, 30.0f - trial));
        CHECK(path.found);
        expansions += path.expansions;
    }
    std::printf("open field mean expansions %u\n", expansions / 20);
    CHECK(expansions / 20 < 6000);
}

void testArrivalAndCorners() {
    NavMesh mesh = makeArena(kArenaHalf);
    mesh.addObstacle(boxAt(0.0f, 0.0f, 3.0f));
    mesh.rebuildDirty();
    std::vector<NavAgent> agents(1);
    agents[0].position = glm::vec3(-10.0f, 0.0f, 0.0f);
    navSetTarget(mesh, agents[0], glm::vec3(10.0f, 0.0f, 0.5f));
    float maxLateSpeed = 0.0f;
    for (int step = 0; step < 900 && agents[0].state == AgentState::Moving; ++step) {
        navStepAgents(mesh, agents, kStep);
        if (glm::length(agents[0].target - agents[0].position) < 0.3f) maxLateSpeed = std::max(maxLateSpeed, glm::length(agents[0].velocity));
    }
    CHECK(agents[0].state == AgentState::Arrived);
    CHECK(maxLateSpeed < 1.5f);
}

void testDeterminism() {
    uint64_t hashes[2];
    for (uint64_t& hash : hashes) {
        NavMesh mesh = makeArena(kArenaHalf);
        std::vector<NavAgent> agents = makeAgents(mesh, 300, 20.0f, 5);
        NavCrowdConfig config;
        config.pathExpansionBudget = 4000;
        NavCrowd crowd(config);
        for (int step = 0; step < 200; ++step) {
            if (step == 50) {
                mesh.addObstacle(boxAt(0.0f, 0.0f, 2.0f));
                mesh.rebuildDirty();
            }
            crowd.step(mesh, agents, kStep);
        }
        hash = hashAgents(agents);
    }
    CHECK(hashes[0] == hashes[1]);
}

void testScaling() {
    constexpr float half = 60.0f;
    NavMesh mesh = makeArena(half);
    for (const size_t count : {size_t(1000), size_t(5000)}) {
        std::vector<NavAgent> agents = makeAgents(mesh, count, half - 4.0f, 21);
        NavCrowd crowd;
        for (int warm = 0; warm < 5; ++warm) crowd.step(mesh, agents, kStep);
        constexpr int steps = 30;
        const auto begin = std::chrono::steady_clock::now();
        for (int step = 0; step < steps; ++step) crowd.step(mesh, agents, kStep);
        const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / steps;
        std::printf("nav scaling: %zu agents %.3f ms/step\n", count, milliseconds);
        CHECK(milliseconds < 100.0);
    }
}

}

int main() {
    testSlicedMatchesSync();
    testLocalInvalidation();
    testBudgetedPaths();
    testPathCache();
    testAvoidance();
    testOptimalVersusDijkstra();
    testPathSpeed();
    testArrivalAndCorners();
    testDeterminism();
    testScaling();
    return testsupport::finish();
}
