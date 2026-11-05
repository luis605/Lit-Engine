#include "TestSupport.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.glm;
import Engine.Json;
import Engine.IK;
import Engine.History;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFrame = 1.0f / 60.0f;

IkChainState straightChain(int joints, float boneLength) {
    IkChainState chain;
    chain.count = joints;
    for (int index = 0; index < joints; ++index) chain.points[static_cast<size_t>(index)] = glm::vec3(0.0f, -boneLength * static_cast<float>(index), 0.0f);
    ikMeasureLengths(chain);
    return chain;
}

bool lengthsPreserved(const IkChainState& chain) {
    for (int bone = 0; bone + 1 < chain.count; ++bone) {
        if (std::abs(glm::length(chain.points[static_cast<size_t>(bone + 1)] - chain.points[static_cast<size_t>(bone)]) - chain.lengths[static_cast<size_t>(bone)]) > 1.0e-3f) return false;
    }
    return true;
}

void testSolvers() {
    const glm::vec3 target(1.2f, -1.5f, 0.8f);
    IkSettings settings;
    settings.iterations = 40;
    IkChainState fabrik = straightChain(6, 0.5f);
    const IkResult fabrikResult = solveFabrik(fabrik, target, settings);
    CHECK(fabrikResult.reached);
    CHECK(glm::length(fabrik.tip() - target) < 2.0e-3f);
    CHECK(lengthsPreserved(fabrik));
    CHECK(glm::length(fabrik.root()) < 1.0e-5f);

    IkChainState ccd = straightChain(6, 0.5f);
    const IkResult ccdResult = solveCcd(ccd, target, settings);
    CHECK(ccdResult.reached);
    CHECK(lengthsPreserved(ccd));

    IkChainState far = straightChain(4, 0.5f);
    const glm::vec3 beyond(0.0f, 4.0f, 0.0f);
    solveFabrik(far, beyond, settings);
    CHECK(std::abs(glm::length(far.tip()) - 1.5f) < 1.0e-3f);
    CHECK(glm::dot(glm::normalize(far.tip()), glm::vec3(0.0f, 1.0f, 0.0f)) > 0.999f);

    IkChainState two = straightChain(3, 0.7f);
    two.points[1] = glm::vec3(0.1f, -0.7f, 0.0f);
    IkSettings poled;
    poled.hasPole = true;
    poled.pole = glm::vec3(0.0f, -0.7f, 5.0f);
    const IkResult twoResult = solveTwoBoneAnalytic(two, glm::vec3(0.3f, -0.9f, 0.0f), poled);
    CHECK(twoResult.reached);
    CHECK(two.points[1].z > 0.1f);
    CHECK(lengthsPreserved(two));
    poled.pole = glm::vec3(0.0f, -0.7f, -5.0f);
    solveTwoBoneAnalytic(two, glm::vec3(0.3f, -0.9f, 0.0f), poled);
    CHECK(two.points[1].z < -0.1f);
}

void testConstraints() {
    IkChainState hinge = straightChain(4, 0.5f);
    for (int bone = 1; bone < 3; ++bone) hinge.limits[static_cast<size_t>(bone)] = IkJointLimit{true, 0.0f, kPi * 0.5f, kPi};
    IkSettings settings;
    settings.iterations = 40;
    settings.hasPole = true;
    settings.pole = glm::vec3(0.0f, -0.7f, 3.0f);
    solveFabrik(hinge, glm::vec3(0.4f, -0.8f, 0.0f), settings);
    CHECK(lengthsPreserved(hinge));
    for (int bone = 1; bone < 3; ++bone) {
        const glm::vec3 previous = glm::normalize(hinge.points[static_cast<size_t>(bone)] - hinge.points[static_cast<size_t>(bone - 1)]);
        const glm::vec3 current = glm::normalize(hinge.points[static_cast<size_t>(bone + 1)] - hinge.points[static_cast<size_t>(bone)]);
        const float angle = ikSignedAngle(previous, current, hinge.bendAxis);
        CHECK(angle > -1.0e-2f && angle < kPi * 0.5f + 1.0e-2f);
        CHECK(std::abs(glm::dot(current, hinge.bendAxis)) < 1.0e-2f);
    }

    IkChainState cone = straightChain(5, 0.5f);
    const float limit = kPi / 6.0f;
    for (int bone = 1; bone < 4; ++bone) cone.limits[static_cast<size_t>(bone)].cone = limit;
    IkSettings coneSettings;
    coneSettings.iterations = 30;
    solveFabrik(cone, glm::vec3(1.5f, -0.5f, 0.0f), coneSettings);
    for (int bone = 1; bone < 4; ++bone) {
        const glm::vec3 previous = glm::normalize(cone.points[static_cast<size_t>(bone)] - cone.points[static_cast<size_t>(bone - 1)]);
        const glm::vec3 current = glm::normalize(cone.points[static_cast<size_t>(bone + 1)] - cone.points[static_cast<size_t>(bone)]);
        CHECK(std::acos(std::clamp(glm::dot(previous, current), -1.0f, 1.0f)) <= limit + 1.0e-2f);
    }
    CHECK(lengthsPreserved(cone));

    IkChainState ccdCone = straightChain(5, 0.5f);
    for (int bone = 1; bone < 4; ++bone) ccdCone.limits[static_cast<size_t>(bone)].cone = limit;
    solveCcd(ccdCone, glm::vec3(1.5f, -0.5f, 0.0f), coneSettings);
    for (int bone = 1; bone < 4; ++bone) {
        const glm::vec3 previous = glm::normalize(ccdCone.points[static_cast<size_t>(bone)] - ccdCone.points[static_cast<size_t>(bone - 1)]);
        const glm::vec3 current = glm::normalize(ccdCone.points[static_cast<size_t>(bone + 1)] - ccdCone.points[static_cast<size_t>(bone)]);
        CHECK(std::acos(std::clamp(glm::dot(previous, current), -1.0f, 1.0f)) <= limit + 2.0e-2f);
    }
}

void testChainComponent() {
    World world;
    std::vector<EntityHandle> joints;
    EntityHandle parent = NULL_ENTITY;
    for (int index = 0; index < 4; ++index) {
        EntityDesc desc;
        desc.name = "joint" + std::to_string(index);
        desc.position = index == 0 ? glm::vec3(0.0f) : glm::vec3(0.0f, -0.5f, 0.0f);
        desc.parent = parent;
        parent = world.create(desc);
        joints.push_back(parent);
    }
    EntityDesc targetDesc;
    targetDesc.name = "target";
    targetDesc.position = glm::vec3(0.8f, -1.0f, 0.3f);
    const EntityHandle target = world.create(targetDesc);
    createIkChain(world, joints, target);
    const IkFrameStats first = updateIk(world, kFrame);
    CHECK(first.solved == 1);
    CHECK(glm::length(world.getWorldPosition(joints.back()) - world.getWorldPosition(target)) < 5.0e-3f);
    const IkFrameStats second = updateIk(world, kFrame);
    CHECK(second.solved == 0 && second.skipped == 1);
    world.setWorldPosition(target, glm::vec3(-0.5f, -1.1f, 0.2f));
    CHECK(updateIk(world, kFrame).solved == 1);
    CHECK(glm::length(world.getWorldPosition(joints.back()) - world.getWorldPosition(target)) < 5.0e-3f);
    for (int index = 0; index < 3; ++index) {
        const float length = glm::length(world.getWorldPosition(joints[static_cast<size_t>(index + 1)]) - world.getWorldPosition(joints[static_cast<size_t>(index)]));
        CHECK(std::abs(length - 0.5f) < 1.0e-3f);
    }
    IkChain* chain = world.get<IkChain>(joints.front());
    chain->weight = 0.5f;
    world.setWorldPosition(target, glm::vec3(0.9f, -0.9f, 0.0f));
    updateIk(world, kFrame);
    CHECK(glm::length(world.getWorldPosition(joints.back()) - world.getWorldPosition(target)) > 5.0e-3f);
}

float groundHeight(float z) {
    if (z > -2.0f) return 0.0f;
    if (z > -4.0f) return 0.15f;
    if (z > -6.0f) return 0.15f + 0.1f * (-4.0f - z);
    return 0.35f;
}

bool unevenGround(const glm::vec3& position, float& height, glm::vec3& normal) {
    height = groundHeight(position.z);
    const float slope = position.z < -4.0f && position.z > -6.0f ? 0.1f : 0.0f;
    normal = glm::normalize(glm::vec3(0.0f, 1.0f, slope));
    return true;
}

bool anyAdjacentSwinging(const Walker& walker) {
    for (size_t first = 0; first < walker.legs.size(); ++first) {
        for (size_t second = first + 1; second < walker.legs.size(); ++second) {
            if (!walker.legs[first].swinging || !walker.legs[second].swinging) continue;
            const WalkerLeg& a = walker.legs[first];
            const WalkerLeg& b = walker.legs[second];
            const int other = static_cast<int>(second);
            const int self = static_cast<int>(first);
            if (a.sameSideNext == other || a.sameSidePrevious == other || a.mirror == other || b.sameSideNext == self || b.sameSidePrevious == self || b.mirror == self) return true;
        }
    }
    return false;
}

void testSpiderWalk() {
    World world;
    SpiderOptions options;
    const EntityHandle body = makeSpider(world, glm::vec3(0.0f), options);
    Walker* walker = world.get<Walker>(body);
    CHECK(walker && walker->legs.size() == 8);
    walker->ground = unevenGround;
    const glm::vec3 goal(0.0f, 0.0f, -8.0f);
    setWalkerTarget(world, body, goal);

    std::vector<glm::vec3> previousPlanted(8);
    std::vector<uint8_t> previousSwinging(8, 1);
    int adjacentViolations = 0;
    int slideViolations = 0;
    int reachViolations = 0;
    int tipViolations = 0;
    size_t maxSwinging = 0;
    float worstHeightError = 0.0f;
    for (int frame = 0; frame < 60 * 30; ++frame) {
        updateWalkers(world, kFrame);
        updateIk(world, kFrame);
        walker = world.get<Walker>(body);
        if (anyAdjacentSwinging(*walker)) ++adjacentViolations;
        size_t swinging = 0;
        for (size_t index = 0; index < walker->legs.size(); ++index) {
            const WalkerLeg& leg = walker->legs[index];
            swinging += leg.swinging ? 1 : 0;
            const IkChain* chain = world.get<IkChain>(leg.chain);
            const glm::vec3 hip = world.getWorldPosition(chain->joints.front());
            const glm::vec3 tip = world.getWorldPosition(chain->joints.back());
            if (glm::length(leg.position - hip) > leg.reach * 1.02f) ++reachViolations;
            if (glm::length(tip - leg.position) > 0.03f) ++tipViolations;
            if (!leg.swinging && !previousSwinging[index] && glm::length(leg.planted - previousPlanted[index]) > 1.0e-4f) ++slideViolations;
            if (!leg.swinging && !previousSwinging[index] && glm::length(tip - previousPlanted[index]) > 0.03f) ++slideViolations;
            previousPlanted[index] = leg.planted;
            previousSwinging[index] = leg.swinging ? 1 : 0;
        }
        maxSwinging = std::max(maxSwinging, swinging);
        if (frame > 120) {
            const glm::vec3 position = world.getWorldPosition(body);
            worstHeightError = std::max(worstHeightError, std::abs(position.y - groundHeight(position.z) - walker->bodyHeight));
        }
    }
    const glm::vec3 finalPosition = world.getWorldPosition(body);
    CHECK(glm::length(glm::vec3(finalPosition.x - goal.x, 0.0f, finalPosition.z - goal.z)) < 0.5f);
    CHECK(adjacentViolations == 0);
    CHECK(slideViolations == 0);
    CHECK(reachViolations == 0);
    CHECK(tipViolations == 0);
    CHECK(maxSwinging >= 1 && maxSwinging <= 4);
    CHECK(worstHeightError < 0.25f);
    CHECK(finalPosition.y > 0.35f + walker->bodyHeight - 0.2f);
}

void testColliderGround() {
    World world;
    const EntityHandle floor = world.create("floor");
    world.setPosition(floor, glm::vec3(0.0f, -0.5f, 0.0f));
    world.add<BoxCollider>(floor, glm::vec3(50.0f, 0.5f, 50.0f));
    const EntityHandle step = world.create("step");
    world.setPosition(step, glm::vec3(0.0f, 0.1f, -4.0f));
    world.add<BoxCollider>(step, glm::vec3(10.0f, 0.1f, 1.0f));
    const EntityHandle body = makeWalkerFromRecipe(world, glm::vec3(0.0f), "quadruped length=1.0 speed=1.0");
    Walker* walker = world.get<Walker>(body);
    CHECK(walker && walker->legs.size() == 4);
    setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -6.0f));
    float highest = 0.0f;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        updateWalkers(world, kFrame);
        updateIk(world, kFrame);
        highest = std::max(highest, world.getWorldPosition(body).y);
    }
    CHECK(world.getWorldPosition(body).z < -5.0f);
    CHECK(highest > walker->bodyHeight + 0.1f);
    CHECK(std::abs(world.getWorldPosition(body).y - walker->bodyHeight) < 0.1f);
}

void testHierarchyAndJson() {
    World world;
    const EntityHandle spider = makeSpider(world, glm::vec3(0.0f), SpiderOptions{.legs = 6, .segments = 3});
    const std::vector<std::vector<EntityHandle>> legs = detectLegs(world, spider);
    CHECK(legs.size() == 6);
    CHECK(legs.front().size() == 4);
    const Json described = rigToJson(world, spider);
    CHECK(described.get("chains") && described.get("chains")->size() == 6);

    World loaded;
    const std::optional<Json> rig = Json::parse(R"({"name":"s","legs":4,"segments":2,"legLength":0.8,"walker":{"maxSpeed":2.0}})");
    CHECK(rig.has_value());
    std::string error;
    const std::optional<EntityHandle> body = loadRigJson(loaded, *rig, glm::vec3(1.0f, 0.0f, 0.0f), &error);
    CHECK(body.has_value());
    CHECK(loaded.get<Walker>(*body)->legs.size() == 4);
    CHECK(std::abs(loaded.get<Walker>(*body)->maxSpeed - 2.0f) < 1.0e-4f);
    CHECK(!loadRigJson(loaded, Json(5), glm::vec3(0.0f)).has_value());
}

}

void testSpawnUndoRedo() {
    World world;
    History history(world);
    const size_t baseline = world.aliveCount();
    const glm::vec3 position(1.0f, 0.0f, 2.0f);
    const std::vector<EntityHandle> created = history.spawnRebuildable("Spawn Spider", [&] { return collectRigEntities(world, makeSpider(world, position, SpiderOptions{})); });
    const EntityHandle spider = created.front();
    const size_t spawned = world.aliveCount();
    CHECK(spawned > baseline);
    CHECK(history.canUndo());

    CHECK(history.undo());
    CHECK(world.aliveCount() == baseline);
    CHECK(!world.isAlive(spider));

    CHECK(history.redo());
    CHECK(world.aliveCount() == spawned);
    const EntityHandle restored = history.resolve(spider);
    CHECK(world.isAlive(restored));
    Walker* walker = world.get<Walker>(restored);
    CHECK(walker && walker->legs.size() == 8);
    CHECK(detectLegs(world, restored).size() == 8);
    CHECK(history.undo());
    CHECK(world.aliveCount() == baseline);
    CHECK(history.redo());
    CHECK(world.aliveCount() == spawned);
}

void testChainUndoRedo() {
    World world;
    History history(world);
    EntityDesc root;
    root.name = "root";
    const EntityHandle first = world.create(root);
    EntityDesc childDesc;
    childDesc.name = "child";
    childDesc.parent = first;
    childDesc.position = glm::vec3(0.0f, 1.0f, 0.0f);
    const EntityHandle second = world.create(childDesc);
    const std::vector<EntityHandle> joints{first, second};
    const size_t baseline = world.aliveCount();
    const EntityHandle target = spawnIkChain(history, joints, "root.target", glm::vec3(1.0f, 1.0f, 0.0f));
    CHECK(world.isAlive(target));
    CHECK(world.get<IkChain>(first) != nullptr);
    CHECK(world.aliveCount() == baseline + 1);
    CHECK(history.undoCount() == 1);

    CHECK(history.undo());
    CHECK(world.get<IkChain>(first) == nullptr);
    CHECK(world.aliveCount() == baseline);

    CHECK(history.redo());
    const IkChain* chain = world.get<IkChain>(first);
    CHECK(chain != nullptr);
    CHECK(world.aliveCount() == baseline + 1);
    CHECK(chain && world.isAlive(chain->target));
    CHECK(history.undo());
    CHECK(world.get<IkChain>(first) == nullptr);
}

int main() {
    testSolvers();
    testConstraints();
    testChainComponent();
    testSpiderWalk();
    testColliderGround();
    testHierarchyAndJson();
    testSpawnUndoRedo();
    testChainUndoRedo();
    return testsupport::finish();
}
