#include "ReviewSuites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

import Engine.World;
import Engine.SceneValidator;
import Engine.WorldHash;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr double kQuietDouble = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfDouble = std::numeric_limits<double>::infinity();

bool hasCode(const std::vector<ValidationIssue>& issues, const char* code) {
    for (const ValidationIssue& issue : issues) {
        if (issue.code == code) return true;
    }
    return false;
}

void testValidatorLights() {
    World world;
    const EntityHandle nanLight = world.create("nanlight");
    LightComponent* light = world.add<LightComponent>(nanLight);
    light->intensity = kQuiet;
    CHECK(hasCode(validateScene(world), "light.dark"));
    light->intensity = 1.0f;
    light->range = kQuiet;
    CHECK(hasCode(validateScene(world), "light.range"));
    light->range = 10.0f;
    light->color = glm::vec3(kQuiet);
    CHECK(hasCode(validateScene(world), "light.dark"));
}

void testValidatorTransforms() {
    World world;
    const EntityHandle mirrored = world.create("mirrored", 1, glm::vec3(0.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
    const EntityHandle broken = world.create("broken", 1, glm::vec3(kQuiet, 0.0f, 0.0f));
    (void)mirrored;
    (void)broken;
    const auto issues = validateScene(world);
    CHECK(hasCode(issues, "transform.nan"));
    CHECK(!hasCode(issues, "scale.zero"));
    CHECK(!hasCode(issues, "id.duplicate"));
}

void testValidatorLimits() {
    World empty;
    CHECK(validateScene(empty).empty());
    World world;
    for (int index = 0; index < 50; ++index) world.create("same", 7, glm::vec3(kQuiet));
    ValidatorLimits limits;
    limits.maxIssues = 0;
    CHECK(validateScene(world, limits).empty());
    limits.maxIssues = 5;
    limits.checkAssets = true;
    CHECK(validateScene(world, limits).size() == 5);
    limits.maxIssues = 100000;
    limits.meshCount = 3;
    limits.materialCount = 1;
    const auto issues = validateScene(world, limits);
    CHECK(hasCode(issues, "mesh.missing"));
    CHECK(countIssues(issues, Severity::Error) >= 50);
    const EntityHandle parent = world.create("parent");
    const EntityHandle child = world.create("child", NO_MESH, glm::vec3(0.0f), glm::vec3(1.0f), parent);
    world.destroy(parent);
    (void)child;
    (void)validateScene(world, limits);
}

void testExplainFrameNonFinite() {
    FrameFacts facts;
    for (const double bad : {kQuietDouble, kInfDouble, -kInfDouble, -5.0, 0.0, 1.0e300}) {
        facts.cpuMs = bad;
        facts.gpuMs = bad;
        facts.targetMs = bad;
        facts.gpuPasses = {{"Shadows", bad}, {"", 1.0}};
        facts.cpuScopes = {{"update", bad}};
        const auto findings = explainFrame(facts);
        CHECK(!findings.empty());
        CHECK(!formatFindings(findings).empty());
    }
    facts = FrameFacts{};
    facts.gpuMs = 10.0;
    facts.gpuPasses = {{"Shadows", 6.0}, {"Lighting", 3.0}};
    const auto findings = explainFrame(facts);
    CHECK(findings.front().title.find("Shadows") != std::string::npos);
    FrameFacts empty;
    CHECK(explainFrame(empty).size() == 1);
    FrameFacts huge;
    huge.scene.drawables = static_cast<size_t>(-1);
    huge.scene.shadowCasters = static_cast<size_t>(-1);
    huge.streamingHitches = static_cast<uint64_t>(-1);
    huge.shadowResolution = std::numeric_limits<int>::min();
    CHECK(!explainFrame(huge).empty());
}

World& build(World& world, float offset) {
    world.create("a", 1, glm::vec3(1.0f + offset, 2.0f, 3.0f));
    world.create("b", 1, glm::vec3(4.0f, 5.0f, 6.0f));
    return world;
}

void testHashBehaviour() {
    World first;
    World second;
    build(first, 0.0f);
    build(second, 0.0f);
    CHECK(hashWorldState(first) == hashWorldState(second));
    World shifted;
    build(shifted, 0.001f);
    CHECK(hashWorldState(first) != hashWorldState(shifted));
    World negativeZero;
    World positiveZero;
    negativeZero.create("z", 1, glm::vec3(-0.0f, 0.0f, 0.0f));
    positiveZero.create("z", 1, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(hashWorldState(negativeZero) == hashWorldState(positiveZero));
    CHECK(quantiseForHash(1.0e30f) == quantiseForHash(2.0e30f));
    CHECK(quantiseForHash(-1.0e30f) == -quantiseForHash(1.0e30f));
    CHECK(quantiseForHash(kQuiet) == quantiseForHash(std::numeric_limits<float>::infinity()));
    CHECK(quantiseForHash(0.00004f) == 0 && quantiseForHash(-0.00004f) == 0);
    CHECK(quantiseForHash(0.00006f) == 1 && quantiseForHash(-0.00006f) == -1);
    World withNan;
    withNan.create("n", 1, glm::vec3(kQuiet));
    (void)hashWorldState(withNan);

    World rotatedFirst;
    World rotatedSecond;
    const EntityHandle one = rotatedFirst.create("r", 1);
    const EntityHandle two = rotatedSecond.create("r", 1);
    rotatedFirst.setRotation(one, glm::quat(0.0f, 1.0f, 0.0f, 0.0f));
    rotatedSecond.setRotation(two, glm::quat(0.0f, -1.0f, 0.0f, 0.0f));
    CHECK(hashWorldState(rotatedFirst) == hashWorldState(rotatedSecond));

    StateHasher left;
    StateHasher right;
    left.addText("ab");
    left.addText("c");
    right.addText("a");
    right.addText("bc");
    CHECK(left.value() != right.value());
}

}

void runReviewToolsTests() {
    testValidatorLights();
    testValidatorTransforms();
    testValidatorLimits();
    testExplainFrameNonFinite();
    testHashBehaviour();
}
