#include "ReviewSuites.hpp"
#include "TestSupport.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;
import Engine.IK;
import Engine.Json;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr float kFrame = 1.0f / 60.0f;

bool finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

bool chainFinite(const IkChainState& chain) {
    for (int index = 0; index < chain.count; ++index) {
        if (!finite(chain.points[static_cast<size_t>(index)])) return false;
    }
    return true;
}

IkChainState straight(int joints, float length) {
    IkChainState chain;
    chain.count = joints;
    for (int index = 0; index < joints; ++index) chain.points[static_cast<size_t>(index)] = glm::vec3(0.0f, -length * static_cast<float>(index), 0.0f);
    ikMeasureLengths(chain);
    return chain;
}

void testSolversRejectNonFinite() {
    IkSettings settings;
    for (const float bad : {kQuiet, std::numeric_limits<float>::infinity()}) {
        for (int joints = 2; joints <= 6; ++joints) {
            IkChainState fabrik = straight(joints, 0.5f);
            solveFabrik(fabrik, glm::vec3(bad, 1.0f, 0.0f), settings);
            CHECK(chainFinite(fabrik));
            IkChainState ccd = straight(joints, 0.5f);
            solveCcd(ccd, glm::vec3(0.0f, bad, 0.0f), settings);
            CHECK(chainFinite(ccd));
            IkChainState any = straight(joints, 0.5f);
            solveIk(any, glm::vec3(0.5f, -0.5f, bad), settings);
            CHECK(chainFinite(any));
        }
    }
    IkChainState poled = straight(4, 0.5f);
    settings.hasPole = true;
    settings.pole = glm::vec3(kQuiet);
    solveFabrik(poled, glm::vec3(0.5f, -1.0f, 0.0f), settings);
    CHECK(chainFinite(poled));
}

void testInvertedLimits() {
    IkChainState chain = straight(4, 0.5f);
    for (int bone = 1; bone < 3; ++bone) chain.limits[static_cast<size_t>(bone)] = IkJointLimit{true, 1.0f, -1.0f, kIkPi};
    IkSettings settings;
    settings.hasPole = true;
    settings.pole = glm::vec3(0.0f, -0.5f, 2.0f);
    solveFabrik(chain, glm::vec3(0.5f, -1.0f, 0.0f), settings);
    CHECK(chainFinite(chain));
    IkChainState negative = straight(4, 0.5f);
    for (int bone = 1; bone < 3; ++bone) negative.limits[static_cast<size_t>(bone)].cone = -0.5f;
    solveFabrik(negative, glm::vec3(0.5f, -1.0f, 0.0f), settings);
    CHECK(chainFinite(negative));
}

void testZeroAndDegenerateChains() {
    IkChainState zero;
    zero.count = 4;
    IkSettings settings;
    solveFabrik(zero, glm::vec3(1.0f, 0.0f, 0.0f), settings);
    CHECK(chainFinite(zero));
    solveCcd(zero, glm::vec3(1.0f, 0.0f, 0.0f), settings);
    CHECK(chainFinite(zero));
    IkChainState two = straight(3, 0.5f);
    solveTwoBoneAnalytic(two, two.root(), settings);
    CHECK(chainFinite(two));
    IkChainState one = straight(1, 0.5f);
    solveIk(one, glm::vec3(1.0f), settings);
    IkChainState none;
    solveIk(none, glm::vec3(1.0f), settings);
    settings.iterations = 0;
    IkChainState reach = straight(5, 0.5f);
    solveFabrik(reach, glm::vec3(0.3f, -1.0f, 0.0f), settings);
    CHECK(chainFinite(reach));
    settings.iterations = -5;
    solveCcd(reach, glm::vec3(0.3f, -1.0f, 0.0f), settings);
    CHECK(chainFinite(reach));
}

EntityHandle makeJoints(World& world, int count, std::vector<EntityHandle>& joints) {
    EntityHandle parent = NULL_ENTITY;
    for (int index = 0; index < count; ++index) {
        EntityDesc desc;
        desc.name = "joint";
        desc.position = index == 0 ? glm::vec3(0.0f) : glm::vec3(0.0f, -0.5f, 0.0f);
        desc.parent = parent;
        parent = world.create(desc);
        joints.push_back(parent);
    }
    return parent;
}

void testChainComponentNonFiniteTarget() {
    World world;
    std::vector<EntityHandle> joints;
    makeJoints(world, 4, joints);
    const EntityHandle target = world.create("target", NO_MESH, glm::vec3(0.5f, -1.0f, 0.0f));
    createIkChain(world, joints, target);
    updateIk(world, kFrame);
    world.setWorldPosition(target, glm::vec3(kQuiet, 0.0f, 0.0f));
    updateIk(world, kFrame);
    updateIk(world, kFrame);
    for (EntityHandle joint : joints) CHECK(finite(world.getWorldPosition(joint)));
    IkChain* chain = world.get<IkChain>(joints.front());
    chain->polePoint = glm::vec3(kQuiet);
    chain->usePolePoint = true;
    world.setWorldPosition(target, glm::vec3(0.4f, -1.1f, 0.2f));
    updateIk(world, kFrame);
    for (EntityHandle joint : joints) CHECK(finite(world.getWorldPosition(joint)));
}

void testChainEntityDestroyedMidSolve() {
    World world;
    std::vector<EntityHandle> joints;
    makeJoints(world, 3, joints);
    const EntityHandle target = world.create("target", NO_MESH, glm::vec3(0.3f, -0.7f, 0.0f));
    createIkChain(world, joints, target);
    updateIk(world, kFrame);
    world.destroy(joints[1]);
    updateIk(world, kFrame);
    world.destroy(target);
    updateIk(world, kFrame);
    CHECK(world.validate().empty());
    const std::vector<EntityHandle> tooMany(static_cast<size_t>(kMaxIkJoints) + 3, joints.front());
    CHECK(createIkChain(world, tooMany) == NULL_ENTITY);
    const std::vector<EntityHandle> single{joints.front()};
    CHECK(createIkChain(world, single) == NULL_ENTITY);
}

bool bodyFinite(World& world, EntityHandle body) {
    const Walker* walker = world.get<Walker>(body);
    if (!world.isAlive(body)) return true;
    if (!finite(world.getWorldPosition(body))) return false;
    if (!walker) return true;
    if (!finite(walker->velocity) || !finite(walker->heading) || !finite(walker->smoothedUp)) return false;
    for (const WalkerLeg& leg : walker->legs) {
        if (!finite(leg.position) || !finite(leg.planted)) return false;
    }
    return true;
}

void runFrames(World& world, int frames, float step) {
    for (int frame = 0; frame < frames; ++frame) {
        updateWalkers(world, step);
        updateIk(world, step);
    }
}

void testWalkerNonFiniteSteering() {
    {
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
        setWalkerTarget(world, body, glm::vec3(kQuiet, 0.0f, 3.0f));
        runFrames(world, 30, kFrame);
        CHECK(bodyFinite(world, body));
        setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -3.0f));
        runFrames(world, 30, kFrame);
        CHECK(bodyFinite(world, body));
    }
    {
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
        setWalkerInput(world, body, glm::vec2(kQuiet, 1.0f));
        runFrames(world, 30, kFrame);
        CHECK(bodyFinite(world, body));
    }
    {
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
        setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -3.0f));
        updateWalkers(world, kQuiet);
        runFrames(world, 10, kFrame);
        CHECK(bodyFinite(world, body));
    }
}

void testWalkerTimeSteps() {
    for (const float step : {0.0f, -1.0f, 1.0e-9f, 0.5f, 5.0f, 1000.0f}) {
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
        setWalkerTarget(world, body, glm::vec3(2.0f, 0.0f, -4.0f));
        runFrames(world, 40, step);
        CHECK(bodyFinite(world, body));
    }
    World world;
    const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
    setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -4.0f));
    runFrames(world, 60, kFrame);
    for (int spike = 0; spike < 20; ++spike) {
        runFrames(world, 1, spike % 2 == 0 ? 2.0f : kFrame);
        CHECK(bodyFinite(world, body));
    }
}

void testWalkerLegCounts() {
    for (const int legs : {0, 1, 2, 3, 5, 16, 17, 40, -2}) {
        World world;
        SpiderOptions options;
        options.legs = legs;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f), options);
        if (world.isAlive(body)) setWalkerTarget(world, body, glm::vec3(1.0f, 0.0f, -3.0f));
        runFrames(world, 120, kFrame);
        CHECK(bodyFinite(world, body));
        CHECK(world.validate().empty());
    }
}

void testSpiderOptionsDegenerate() {
    const std::vector<SpiderOptions> variants = {
        SpiderOptions{.segments = 0},
        SpiderOptions{.segments = -1},
        SpiderOptions{.segments = 40},
        SpiderOptions{.legLength = 0.0f},
        SpiderOptions{.legLength = kQuiet},
        SpiderOptions{.legLength = -1.0f},
        SpiderOptions{.heightRatio = 0.0f},
        SpiderOptions{.restRatio = 0.0f},
        SpiderOptions{.stepHeight = kQuiet, .stride = kQuiet},
    };
    for (const SpiderOptions& options : variants) {
        
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f), options);
        if (world.isAlive(body)) setWalkerTarget(world, body, glm::vec3(1.0f, 0.0f, -3.0f));
        runFrames(world, 60, kFrame);
        CHECK(bodyFinite(world, body));
        world.forEach([&](EntityHandle entity) { CHECK(finite(world.getWorldPosition(entity))); });
    }
}

void testWalkerEntityDestroyedMidStep() {
    for (int victim = 0; victim < 4; ++victim) {
        World world;
        const EntityHandle body = makeSpider(world, glm::vec3(0.0f), SpiderOptions{.legs = 4});
        setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -6.0f));
        runFrames(world, 50, kFrame);
        const Walker* walker = world.get<Walker>(body);
        const WalkerLeg leg = walker->legs[1];
        if (victim == 0) world.destroy(leg.foot);
        if (victim == 1) world.destroy(leg.chain);
        if (victim == 2) world.destroy(world.get<IkChain>(leg.chain)->joints[1]);
        if (victim == 3) world.destroy(world.get<IkChain>(leg.chain)->joints.back());
        runFrames(world, 100, kFrame);
        CHECK(bodyFinite(world, body));
        CHECK(world.validate().empty());
    }
    World world;
    const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
    setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -6.0f));
    runFrames(world, 30, kFrame);
    world.destroy(body);
    runFrames(world, 30, kFrame);
    CHECK(world.validate().empty());
}

void testWalkerZeroScaleBody() {
    World world;
    const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
    runFrames(world, 5, kFrame);
    world.setScale(body, glm::vec3(0.0f));
    setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -6.0f));
    runFrames(world, 30, kFrame);
    CHECK(bodyFinite(world, body));
}

void testWalkerSpeedZero() {
    World world;
    const EntityHandle body = makeSpider(world, glm::vec3(0.0f));
    Walker* walker = world.get<Walker>(body);
    walker->maxSpeed = 0.0f;
    walker->stride = 0.0f;
    walker->maxSwingTime = 0.0f;
    walker->minSwingTime = 0.0f;
    walker->swingSpeed = 0.0f;
    walker->slowRadius = 0.0f;
    walker->acceleration = 0.0f;
    setWalkerTarget(world, body, glm::vec3(0.0f, 0.0f, -5.0f));
    runFrames(world, 200, kFrame);
    CHECK(bodyFinite(world, body));
    walker = world.get<Walker>(body);
    walker->maxSpeed = 1.0e9f;
    walker->turnRate = kQuiet;
    runFrames(world, 50, kFrame);
    CHECK(bodyFinite(world, body));
}

void testIterationBudgetIsBounded() {
    const auto start = std::chrono::steady_clock::now();
    IkSettings settings;
    settings.iterations = std::numeric_limits<int>::max();
    settings.tolerance = 0.0f;
    IkChainState fabrik = straight(5, 0.5f);
    solveFabrik(fabrik, glm::vec3(0.3f, -1.0f, 0.2f), settings);
    IkChainState ccd = straight(5, 0.5f);
    settings.tolerance = kQuiet;
    solveCcd(ccd, glm::vec3(0.3f, -1.0f, 0.2f), settings);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 2.0);
    CHECK(chainFinite(fabrik) && chainFinite(ccd));
}

void testRigJsonExtremeNumbers() {
    const char* const documents[] = {
        R"({"legs":1e30,"segments":-1e30,"legLength":1e300})",
        R"({"legs":4,"legLength":1e39,"maxSpeed":-1e39,"splay":1e300})",
        R"({"legs":4,"walker":{"maxSpeed":1e300,"stride":-1e300,"stepHeight":1e39}})",
        R"({"legs":3,"chains":[{"joints":["leg0","leg0.1"],"iterations":2000000000,"weight":1e300,"limits":[{"min":1e300,"max":-1e300,"cone":-1e300,"hinge":true}]}]})",
        R"({"legs":2,"chains":[5,null,{"joints":[1,2]},{"joints":"x"},{"joints":[]}]})",
        R"({"hierarchy":5})",
        R"({"hierarchy":"missing","chains":{}})",
    };
    for (const char* text : documents) {
        const auto rig = Json::parse(text);
        CHECK(rig.has_value());
        if (!rig) continue;
        World world;
        std::string error;
        const auto body = loadRigJson(world, *rig, glm::vec3(0.0f), &error);
        if (body && world.isAlive(*body)) setWalkerTarget(world, *body, glm::vec3(1.0f, 0.0f, -2.0f));
        runFrames(world, 30, kFrame);
        world.forEach([&](EntityHandle entity) { CHECK(finite(world.getWorldPosition(entity))); });
        CHECK(world.validate().empty());
    }
}

void testRecipeExtremeTokens() {
    const char* const recipes[] = {
        "spider legs=nan", "spider legs=inf", "spider legs=1e30", "spider length=nan", "spider length=-5",
        "quadruped speed=inf stride=nan step=1e39", "", "   ", "=", "a=b=c", "biped legs=", "spider segments=99999999999", "spider legs=-0",
    };
    for (const char* recipe : recipes) {
        World world;
        const EntityHandle body = makeWalkerFromRecipe(world, glm::vec3(0.0f), recipe);
        if (world.isAlive(body)) setWalkerTarget(world, body, glm::vec3(1.0f, 0.0f, -2.0f));
        runFrames(world, 30, kFrame);
        CHECK(bodyFinite(world, body));
    }
}

}

void runReviewIkTests() {
    testIterationBudgetIsBounded();
    testRigJsonExtremeNumbers();
    testRecipeExtremeTokens();
    testSolversRejectNonFinite();
    testInvertedLimits();
    testZeroAndDegenerateChains();
    testChainComponentNonFiniteTarget();
    testChainEntityDestroyedMidSolve();
    testWalkerNonFiniteSteering();
    testWalkerTimeSteps();
    testWalkerLegCounts();
    testSpiderOptionsDegenerate();
    testWalkerEntityDestroyedMidStep();
    testWalkerZeroScaleBody();
    testWalkerSpeedZero();
}
