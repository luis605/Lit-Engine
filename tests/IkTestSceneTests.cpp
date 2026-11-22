#include "TestSupport.hpp"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.IK;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.glm;
import Engine.IkTestScene;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kFootTolerance = 0.08f;
constexpr uint64_t kWalkTicks = 900;

IkTestKit makeKit() {
    IkTestKit kit;
    kit.material = [](const Material&) { return 0u; };
    return kit;
}

std::unique_ptr<Engine> makeScene() {
    auto engine = std::make_unique<Engine>();
    engine->openHeadless();
    const IkTestKit kit = makeKit();
    buildIkTestScene(*engine, kit);
    enableIkTestPlay(*engine, kit);
    return engine;
}

float groundHeightAt(World& world, const glm::vec3& position) {
    const auto hit = physicsRaycast(world, glm::vec3(position.x, position.y + 3.0f, position.z), glm::vec3(0.0f, -1.0f, 0.0f), 12.0f);
    return hit ? position.y + 3.0f - hit->distance : -1000.0f;
}

int countPlantedFeetOnGround(World& world, EntityHandle body) {
    const Walker* walker = world.get<Walker>(body);
    int onGround = 0;
    for (const WalkerLeg& leg : walker->legs) {
        if (leg.swinging) continue;
        const glm::vec3 foot = world.getWorldPosition(leg.foot);
        if (std::abs(foot.y - groundHeightAt(world, foot)) <= kFootTolerance) ++onGround;
    }
    return onGround;
}

int plantedFeet(World& world, EntityHandle body) {
    int planted = 0;
    for (const WalkerLeg& leg : world.get<Walker>(body)->legs) planted += leg.swinging ? 0 : 1;
    return planted;
}

void testWalkersLandOnTerrain() {
    auto engine = makeScene();
    World& world = engine->world();
    const EntityHandle spider = world.find(kIkTestSpiderName);
    const EntityHandle quadruped = world.find(kIkTestQuadrupedName);
    const EntityHandle biped = world.find(kIkTestBipedName);
    CHECK(world.isAlive(spider) && world.isAlive(quadruped) && world.isAlive(biped));
    CHECK(world.get<Walker>(spider)->legs.size() == 8);
    const glm::vec3 spiderStart = world.getWorldPosition(spider);
    engine->runHeadless(kWalkTicks, kStep);
    CHECK(glm::length(world.getWorldPosition(spider) - spiderStart) > 3.0f);
    for (const EntityHandle body : {spider, quadruped, biped}) {
        const int planted = plantedFeet(world, body);
        CHECK(planted >= 1);
        CHECK(countPlantedFeetOnGround(world, body) == planted);
    }
}

void testChainsFollowTargets() {
    auto engine = makeScene();
    World& world = engine->world();
    engine->runHeadless(120, kStep);
    for (const char* rootName : {"arm", "tentacle", "ccd", "neck"}) {
        const EntityHandle root = world.find(rootName);
        const IkChain* chain = world.get<IkChain>(root);
        CHECK(chain != nullptr);
        CHECK(chain && chain->bound && chain->solved);
    }
    const IkChain* arm = world.get<IkChain>(world.find("arm"));
    const glm::vec3 tip = world.getWorldPosition(arm->joints.back());
    CHECK(glm::length(tip - world.getWorldPosition(arm->target)) < 0.05f);
}

void testSceneRoundTrip() {
    auto engine = makeScene();
    engine->runHeadless(30, kStep);
    const std::string path = testsupport::tempFilePath("lit_ik_test_", "scene.litscene");
    CHECK(engine->world().saveScene(path));
    Engine other;
    other.openHeadless();
    CHECK(other.world().loadScene(path));
    World& saved = engine->world();
    World& loaded = other.world();
    CHECK(loaded.aliveCount() == saved.aliveCount());
    for (const char* name : {kIkTestSpiderName, kIkTestQuadrupedName, kIkTestBipedName}) {
        const Walker* original = saved.get<Walker>(saved.find(name));
        const Walker* restored = loaded.get<Walker>(loaded.find(name));
        CHECK(original && restored);
        CHECK(original && restored && original->legs.size() == restored->legs.size());
        CHECK(original && restored && std::abs(original->stride - restored->stride) < 1.0e-4f);
    }
    for (const char* name : {"arm", "tentacle", "ccd", "neck"}) {
        const IkChain* original = saved.get<IkChain>(saved.find(name));
        const IkChain* restored = loaded.get<IkChain>(loaded.find(name));
        CHECK(original && restored);
        CHECK(original && restored && original->joints.size() == restored->joints.size());
        CHECK(original && restored && original->algorithm == restored->algorithm);
        CHECK(restored && loaded.isAlive(restored->target));
    }
    const auto solveHeight = [&](Engine& scene) {
        const EntityHandle spider = scene.world().find(kIkTestSpiderName);
        scene.runHeadless(60, kStep);
        return scene.world().getWorldPosition(spider).y;
    };
    CHECK(std::isfinite(solveHeight(other)));
}

}

int main(int argumentCount, char** arguments) {
    testWalkersLandOnTerrain();
    testChainsFollowTargets();
    testSceneRoundTrip();
    if (argumentCount > 1) {
        Engine engine;
        engine.openHeadless();
        buildIkTestScene(engine, makeKit());
        CHECK(engine.world().saveScene(arguments[1]));
    }
    return testsupport::finish();
}
