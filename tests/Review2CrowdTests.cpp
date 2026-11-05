#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

import Engine.Nav;
import Engine.Geometry;
import Engine.glm;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr float kStep = 1.0f / 30.0f;
constexpr float kHalf = 30.0f;

NavMesh makeArena() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(kHalf, 0.5f, kHalf));
    NavMesh mesh;
    NavConfig config;
    config.originX = -kHalf;
    config.originZ = -kHalf;
    config.cellSize = 0.5f;
    mesh.configure(config);
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    return mesh;
}

bool finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

glm::vec3 flatGap(const glm::vec3& first, const glm::vec3& second) { return {first.x - second.x, 0.0f, first.z - second.z}; }

NavAgent staleAgent(const glm::vec3& position, const glm::vec3& target) {
    NavAgent agent;
    agent.position = position;
    agent.target = target;
    agent.hasTarget = true;
    agent.state = AgentState::Moving;
    agent.pathVersion = 0;
    return agent;
}

bool pathEndsAt(const NavAgent& agent) {
    return agent.path.found && !agent.path.points.empty() && glm::length(flatGap(agent.path.points.back().position, agent.target)) < 1.5f;
}

void testReorderedAgentsKeepOwnPaths() {
    NavMesh mesh = makeArena();
    NavCrowdConfig config;
    config.pathExpansionBudget = 30;
    NavCrowd crowd(config);
    std::vector<NavAgent> agents;
    agents.push_back(staleAgent({-25.0f, 0.0f, -25.0f}, {25.0f, 0.0f, 25.0f}));
    agents.push_back(staleAgent({25.0f, 0.0f, -25.0f}, {-25.0f, 0.0f, 25.0f}));
    agents.push_back(staleAgent({0.0f, 0.0f, 25.0f}, {0.0f, 0.0f, -25.0f}));
    crowd.step(mesh, agents, kStep);
    CHECK(crowd.stats().pending > 0);
    std::reverse(agents.begin(), agents.end());
    for (int step = 0; step < 400 && crowd.stats().pending > 0; ++step) crowd.step(mesh, agents, kStep);
    CHECK(crowd.stats().pending == 0);
    for (const NavAgent& agent : agents) CHECK(pathEndsAt(agent));
}

void testRetargetWhilePending() {
    NavMesh mesh = makeArena();
    NavCrowdConfig config;
    config.pathExpansionBudget = 30;
    NavCrowd crowd(config);
    std::vector<NavAgent> agents{staleAgent({-25.0f, 0.0f, -25.0f}, {25.0f, 0.0f, 25.0f})};
    crowd.step(mesh, agents, kStep);
    CHECK(crowd.stats().pending == 1);
    navSetTarget(mesh, agents[0], {-25.0f, 0.0f, 25.0f});
    for (int step = 0; step < 400 && crowd.stats().pending > 0; ++step) crowd.step(mesh, agents, kStep);
    CHECK(pathEndsAt(agents[0]));
}

void testIdenticalPositions() {
    NavMesh mesh = makeArena();
    std::vector<NavAgent> agents(60);
    for (NavAgent& agent : agents) {
        agent.position = {1.0f, 0.0f, 1.0f};
        navSetTarget(mesh, agent, {1.0f, 0.0f, 1.0f});
    }
    for (int step = 0; step < 200; ++step) navStepAgents(mesh, agents, kStep);
    for (const NavAgent& agent : agents) CHECK(finite(agent.position) && finite(agent.velocity));
    for (NavAgent& agent : agents) navSetTarget(mesh, agent, {20.0f, 0.0f, -10.0f});
    for (int step = 0; step < 300; ++step) navStepAgents(mesh, agents, kStep);
    bool allFinite = true;
    for (const NavAgent& agent : agents) allFinite = allFinite && finite(agent.position) && finite(agent.velocity);
    CHECK(allFinite);
}

void testZeroAgentsAndPoisonedNeighbors() {
    NavMesh mesh = makeArena();
    std::vector<NavAgent> none;
    navStepAgents(mesh, none, kStep);
    NavCrowd crowd;
    crowd.step(mesh, none, kStep);
    std::vector<NavAgent> agents(4);
    for (size_t index = 0; index < agents.size(); ++index) {
        agents[index].position = {static_cast<float>(index), 0.0f, 0.0f};
        navSetTarget(mesh, agents[index], {10.0f, 0.0f, 10.0f});
    }
    agents[1].position.x = kQuiet;
    agents[2].velocity.x = kQuiet;
    agents[3].radius = kQuiet;
    for (int step = 0; step < 60; ++step) crowd.step(mesh, agents, kStep);
    CHECK(finite(agents[0].position) && finite(agents[0].velocity));
    NavCrowdConfig odd;
    odd.neighborRange = 0.0f;
    odd.cacheEntries = 0;
    crowd.configure(odd);
    std::vector<NavAgent> rest(3);
    for (NavAgent& agent : rest) navSetTarget(mesh, agent, {5.0f, 0.0f, 5.0f});
    for (int step = 0; step < 30; ++step) crowd.step(mesh, rest, kStep);
    CHECK(finite(rest[0].position));
}

}

void runReview2CrowdTests() {
    testReorderedAgentsKeepOwnPaths();
    testRetargetWhilePending();
    testIdenticalPositions();
    testZeroAgentsAndPoisonedNeighbors();
}
