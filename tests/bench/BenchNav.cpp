#include <cstdint>
#include <memory>
#include <vector>

#include "Harness.hpp"

import Engine.Nav;
import Engine.Geometry;
import Engine.glm;
import Engine.Rng;

namespace bench {

namespace {

constexpr float kFloorHalf = 32.0f;
constexpr float kFloorThickness = 0.5f;
constexpr int kPillarGrid = 5;
constexpr float kPillarSpacing = 10.0f;
constexpr float kPillarHalf = 1.0f;
constexpr float kPillarHeight = 2.0f;
constexpr size_t kPathCount = 100;
constexpr size_t kAgentCount = 200;
constexpr float kSpawnRange = 56.0f;
constexpr float kStepSeconds = 1.0f / 30.0f;
constexpr float kHalf = 0.5f;

TriangleSoup buildArena() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -kFloorThickness, 0.0f)), glm::vec3(kFloorHalf, kFloorThickness, kFloorHalf));
    const float origin = -kPillarSpacing * static_cast<float>(kPillarGrid - 1) * kHalf;
    for (int row = 0; row < kPillarGrid; ++row) {
        for (int column = 0; column < kPillarGrid; ++column) {
            const glm::vec3 center(origin + kPillarSpacing * static_cast<float>(column), kPillarHeight * kHalf, origin + kPillarSpacing * static_cast<float>(row));
            soup.addBox(glm::translate(glm::mat4(1.0f), center), glm::vec3(kPillarHalf, kPillarHeight * kHalf, kPillarHalf));
        }
    }
    return soup;
}

std::shared_ptr<NavMesh> buildMesh() {
    auto mesh = std::make_shared<NavMesh>();
    NavConfig config;
    config.originX = -kFloorHalf;
    config.originZ = -kFloorHalf;
    mesh->configure(config);
    mesh->setGeometry(buildArena());
    mesh->rebuildAll();
    return mesh;
}

glm::vec3 randomGround(Pcg32& rng) { return glm::vec3((rng.uniform() - kHalf) * kSpawnRange, 0.0f, (rng.uniform() - kHalf) * kSpawnRange); }

Case buildCase() {
    auto mesh = buildMesh();
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(mesh->tileCount());
    benchCase.iterations = 5;
    benchCase.warmup = 1;
    benchCase.run = [mesh] {
        mesh->rebuildAll();
        sink(static_cast<double>(mesh->walkableCount()));
    };
    return benchCase;
}

Case pathCase() {
    auto mesh = buildMesh();
    auto starts = std::make_shared<std::vector<glm::vec3>>();
    auto goals = std::make_shared<std::vector<glm::vec3>>();
    Pcg32 rng(kSeed + 3);
    for (size_t index = 0; index < kPathCount; ++index) {
        starts->push_back(randomGround(rng));
        goals->push_back(randomGround(rng));
    }
    Case benchCase;
    benchCase.itemsPerIteration = kPathCount;
    benchCase.iterations = 10;
    benchCase.run = [mesh, starts, goals] {
        float length = 0.0f;
        for (size_t index = 0; index < kPathCount; ++index) length += mesh->findPath((*starts)[index], (*goals)[index]).length;
        sink(length);
    };
    return benchCase;
}

Case reachablePathCase() {
    auto mesh = buildMesh();
    auto starts = std::make_shared<std::vector<glm::vec3>>();
    auto goals = std::make_shared<std::vector<glm::vec3>>();
    Pcg32 rng(kSeed + 5);
    NavPath probe;
    while (starts->size() < kPathCount) {
        const glm::vec3 start = randomGround(rng);
        const glm::vec3 goal = randomGround(rng);
        mesh->findPathInto(start, goal, probe);
        if (!probe.found || probe.partial) continue;
        starts->push_back(start);
        goals->push_back(goal);
    }
    Case benchCase;
    benchCase.itemsPerIteration = kPathCount;
    benchCase.iterations = 10;
    benchCase.run = [mesh, starts, goals] {
        float length = 0.0f;
        for (size_t index = 0; index < kPathCount; ++index) length += mesh->findPath((*starts)[index], (*goals)[index]).length;
        sink(length);
    };
    return benchCase;
}

Case stepCase() {
    auto mesh = buildMesh();
    auto agents = std::make_shared<std::vector<NavAgent>>(kAgentCount);
    Pcg32 rng(kSeed + 4);
    for (NavAgent& agent : *agents) {
        agent.position = randomGround(rng);
        navSetTarget(*mesh, agent, randomGround(rng));
    }
    Case benchCase;
    benchCase.itemsPerIteration = kAgentCount;
    benchCase.iterations = 60;
    benchCase.warmup = 5;
    benchCase.run = [mesh, agents] {
        navStepAgents(*mesh, *agents, kStepSeconds);
        sink((*agents)[0].position.x);
    };
    return benchCase;
}

}

void registerNavBenches() {
    add("nav.rebuild_all_64m_arena", buildCase);
    add("nav.find_path_x100", pathCase);
    add("nav.find_path_reachable_x100", reachablePathCase);
    add("nav.step_agents_200", stepCase);
}

}
