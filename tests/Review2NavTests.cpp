#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

import Engine.Nav;
import Engine.Geometry;
import Engine.glm;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kStep = 1.0f / 30.0f;

NavMesh makeArena(float half, NavConfig config = {}) {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(half, 0.5f, half));
    NavMesh mesh;
    config.originX = -half;
    config.originZ = -half;
    mesh.configure(config);
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    return mesh;
}

NavConfig coarse() {
    NavConfig config;
    config.cellSize = 0.5f;
    return config;
}

Aabb boxAt(float x, float z, float half) {
    Aabb box;
    box.grow(glm::vec3(x - half, 0.0f, z - half));
    box.grow(glm::vec3(x + half, 2.0f, z + half));
    return box;
}

bool finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

double secondsSince(std::chrono::steady_clock::time_point start) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }

void testDegenerateConfig() {
    NavConfig bad;
    bad.tileCells = 0;
    bad.cellSize = 0.0f;
    bad.agentRadius = kQuiet;
    bad.agentHeight = -1.0f;
    bad.maxClimb = kInf;
    bad.snapDistance = kQuiet;
    bad.originX = kQuiet;
    NavMesh mesh = makeArena(10.0f, bad);
    CHECK(mesh.config().tileCells >= 1);
    CHECK(mesh.config().cellSize > 0.0f);
    const NavPath path = mesh.findPath({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f});
    CHECK(path.found || mesh.walkableCount() == 0);
}

void testNonFiniteQueries() {
    NavMesh mesh = makeArena(10.0f, coarse());
    const auto start = std::chrono::steady_clock::now();
    CHECK(!mesh.findPath({kQuiet, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}).found);
    CHECK(!mesh.findPath({0.0f, 0.0f, 0.0f}, {kInf, 0.0f, -kInf}).found);
    CHECK(!mesh.findPath({1.0e30f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}).found);
    CHECK(!mesh.nearestNode({0.0f, 0.0f, 0.0f}, kQuiet).has_value());
    CHECK(mesh.nearestNode({0.0f, 0.0f, 0.0f}, 1.0e30f).has_value());
    CHECK(mesh.nearestNode({0.0f, 0.0f, 0.0f}, kInf).has_value());
    CHECK(!mesh.nearestNode({1.0e30f, 0.0f, kQuiet}, 1.0f).has_value());
    CHECK(!mesh.lineWalkable({0.0f, 0.0f, 0.0f}, {kInf, 0.0f, 0.0f}));
    CHECK(!mesh.lineWalkable({0.0f, 0.0f, 0.0f}, {1.0e30f, 0.0f, 0.0f}));
    CHECK(!mesh.groundHeight({kQuiet, kQuiet, kQuiet}).has_value());
    CHECK(!mesh.walkable({kInf, 0.0f, 0.0f}));
    NavPath poisoned;
    poisoned.found = true;
    poisoned.points.push_back({{0.0f, 0.0f, 0.0f}, -1, false});
    poisoned.points.push_back({{kInf, 0.0f, kQuiet}, -1, false});
    CHECK(mesh.pathStale(poisoned, mesh.version()));
    CHECK(secondsSince(start) < 2.0);
}

void testDegenerateObstacles() {
    NavMesh mesh = makeArena(10.0f, coarse());
    const auto start = std::chrono::steady_clock::now();
    Aabb infinite;
    infinite.lo = glm::vec3(-kInf);
    infinite.hi = glm::vec3(kInf);
    const uint32_t infiniteId = mesh.addObstacle(infinite);
    Aabb poisoned;
    poisoned.lo = glm::vec3(kQuiet);
    poisoned.hi = glm::vec3(kQuiet);
    const uint32_t poisonedId = mesh.addObstacle(poisoned);
    mesh.rebuildDirty();
    CHECK(mesh.dirtyCount() == 0);
    CHECK(mesh.walkableCount() == 0);
    CHECK(mesh.removeObstacle(infiniteId));
    CHECK(mesh.removeObstacle(poisonedId));
    mesh.rebuildDirty();
    CHECK(mesh.walkableCount() > 0);
    CHECK(secondsSince(start) < 5.0);
}

void testHugeGeometry() {
    TriangleSoup soup;
    soup.addQuad({-1.0e8f, 0.0f, -1.0e8f}, {1.0e8f, 0.0f, -1.0e8f}, {1.0e8f, 0.0f, 1.0e8f}, {-1.0e8f, 0.0f, 1.0e8f});
    NavMesh mesh;
    const auto start = std::chrono::steady_clock::now();
    mesh.setGeometry(soup);
    CHECK(mesh.dirtyCount() > 0);
    CHECK(mesh.dirtyCount() <= 1024u * 1024u);
    CHECK(secondsSince(start) < 5.0);
}

void testSlicedPathAcrossObstacle() {
    NavMesh mesh = makeArena(20.0f, coarse());
    const glm::vec3 from(-15.0f, 0.0f, 0.0f);
    const glm::vec3 to(15.0f, 0.0f, 0.0f);
    NavPathQuery query;
    mesh.beginPath(query, from, to);
    CHECK(!mesh.advancePath(query, 20));
    mesh.addObstacle(boxAt(0.0f, 0.0f, 3.0f));
    mesh.rebuildDirty();
    int slices = 0;
    while (!mesh.advancePath(query, 200) && slices < 1000) ++slices;
    CHECK(query.done() && query.path().found);
    CHECK(query.version() == mesh.version());
    bool crosses = false;
    for (const NavPathPoint& point : query.path().points) crosses = crosses || (std::fabs(point.position.x) < 2.0f && std::fabs(point.position.z) < 2.0f);
    CHECK(!crosses);
}

void testSameStartGoalAndWall() {
    NavMesh mesh = makeArena(10.0f, coarse());
    mesh.addObstacle(boxAt(0.0f, 0.0f, 2.0f));
    mesh.rebuildDirty();
    const NavPath same = mesh.findPath({-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, -5.0f});
    CHECK(same.found && same.points.size() >= 2 && same.length < 0.5f);
    const NavPath wall = mesh.findPath({-5.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f});
    CHECK(!wall.found || wall.partial || wall.points.size() >= 2);
    const NavPath outside = mesh.findPath({-5.0f, 0.0f, -5.0f}, {500.0f, 0.0f, 500.0f});
    CHECK(!outside.found);
}

void testAgentGuards() {
    NavMesh mesh = makeArena(20.0f, coarse());
    NavAgent agent;
    agent.position = {-10.0f, 0.0f, 0.0f};
    navSetTarget(mesh, agent, {10.0f, 0.0f, 0.0f});
    CHECK(agent.state == AgentState::Moving);
    const glm::vec3 before = agent.position;
    navStepAgent(mesh, agent, kQuiet);
    navStepAgent(mesh, agent, -1.0f);
    navStepAgent(mesh, agent, kInf);
    CHECK(finite(agent.position) && finite(agent.velocity));
    CHECK(agent.position == before);
    agent.position.x = kQuiet;
    navStepAgent(mesh, agent, kStep);
    CHECK(agent.state == AgentState::Failed);
    CHECK(finite(agent.velocity));
    NavAgent nanAgent;
    nanAgent.position = {kQuiet, 0.0f, 0.0f};
    navSetTarget(mesh, nanAgent, {1.0f, 0.0f, 1.0f});
    CHECK(nanAgent.state == AgentState::Failed);
    NavAgent noSpeed;
    noSpeed.position = {-10.0f, 0.0f, 0.0f};
    noSpeed.maxSpeed = 0.0f;
    noSpeed.radius = 0.0f;
    navSetTarget(mesh, noSpeed, {10.0f, 0.0f, 0.0f});
    for (int step = 0; step < 50; ++step) navStepAgent(mesh, noSpeed, kStep);
    CHECK(finite(noSpeed.position));
}

void testConcurrentConstQueries() {
    constexpr int kThreads = 8;
    constexpr int kQueries = 40;
    NavMesh mesh = makeArena(10.0f, coarse());
    NavLink link;
    link.a = {-8.0f, 0.0f, -8.0f};
    link.b = {8.0f, 0.0f, 8.0f};
    link.radius = 1.0f;
    mesh.addLink(link);
    const NavMesh& shared = mesh;
    const NavPath reference = shared.findPath({-8.0f, 0.0f, -8.0f}, {8.0f, 0.0f, 8.0f});
    CHECK(reference.found);
    mesh.addLink(link);
    std::atomic<int> mismatches{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int index = 0; index < kThreads; ++index) {
        threads.emplace_back([&] {
            while (!go.load()) std::this_thread::yield();
            for (int query = 0; query < kQueries; ++query) {
                const NavPath path = shared.findPath({-8.0f, 0.0f, -8.0f}, {8.0f, 0.0f, 8.0f});
                if (!path.found || path.points.size() != reference.points.size()) ++mismatches;
            }
        });
    }
    go.store(true);
    for (std::thread& thread : threads) thread.join();
    CHECK(mismatches.load() == 0);
}

}

void runReview2NavTests() {
    testConcurrentConstQueries();
    testDegenerateConfig();
    testNonFiniteQueries();
    testDegenerateObstacles();
    testHugeGeometry();
    testSlicedPathAcrossObstacle();
    testSameStartGoalAndWall();
    testAgentGuards();
}
